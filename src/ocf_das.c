#include "ocf/ocf_das.h"
#include "ocf/das.h"
#include "ocf_cache_priv.h"
#include "ocf_volume_priv.h"
#include "ocf_request.h"
#include "ocf_env.h"
#include "utils/utils_stats.h"
#include "engine/engine_common.h"
#include "engine/engine_debug.h"
#include "engine/engine_pf.h"

#define OCF_DAS_SEC_TO_NSEC 1000000000
#define MAX_SINGLE_PF (512 * KiB)
#define MIN_LIMITER_CAPACITY 100
#define MIN_LIMITER_LEAK_RATE 20


static int g_ocf_num = 0;

bool das_get_status(void)
{
	return g_ocf_num >= DAS_INITED;
}

static void das_io_cmpl(struct ocf_io *io, int error)
{
	void *data = (void *)ocf_io_get_data(io);

	if (data != NULL) {
		if (io->io_queue->cache) {
			ctx_data_free(io->io_queue->cache->owner, data);
		}
	}
	ocf_io_put(io);
}

void init_das_limiter(ocf_cache_t cache, ocf_core_t core)
{
	uint64_t cache_size = (uint64_t)cache->conf_meta->cachelines * cache->conf_meta->line_size;
	core->das_limiter.last_out_time = env_get_tick_count();
	core->das_limiter.capacity = OCF_MAX(cache_size / KiB / MAX_SINGLE_PF, MIN_LIMITER_CAPACITY);
	/* Set this leak_rate to 1% of the size of the leaky bucket. */
	core->das_limiter.leak_rate = OCF_MAX(core->das_limiter.capacity / 100, MIN_LIMITER_LEAK_RATE);
	core->das_limiter.cur_water = 0;
	ocf_cache_log(cache, log_info, "das limiter: capacity = %u, leak_rate = %u\n",
			core->das_limiter.capacity, core->das_limiter.leak_rate);
}

static bool das_is_limit(ocf_core_t core)
{
	uint32_t water_leaked;

	if (core->das_limiter.capacity == 0) {
		return true;
	}

	if (core->das_limiter.cur_water == 0) {
		core->das_limiter.last_out_time = env_get_tick_count();
		core->das_limiter.cur_water = 1;
		return false;
	}

	water_leaked = (env_get_tick_count() - core->das_limiter.last_out_time) /
			1000000 * core->das_limiter.leak_rate;
	core->das_limiter.cur_water = (core->das_limiter.cur_water > water_leaked) ?
			(core->das_limiter.cur_water - water_leaked) : 0;
	core->das_limiter.last_out_time = env_get_tick_count();
	if (core->das_limiter.cur_water < core->das_limiter.capacity) {
		core->das_limiter.cur_water += 1;
		return false;
	} else {
		return true;
	}
}

#define SCAN_CACHE_LINES 1
#define MAX_PF_MB (16)
static uint32_t get_pf_req_info(struct ocf_request *req, uint32_t len, int dir)
{
	uint64_t addr;
	/* 0 can't be a valid prefetch address */
	uint64_t first_miss_addr = 0;
	uint64_t left;
	uint64_t skip = ((len > (SCAN_CACHE_LINES * req->byte_length)) ?
		(SCAN_CACHE_LINES * req->byte_length) : req->byte_length);
	uint64_t hits = 0;
	uint64_t miss = 0;
	uint32_t scanned;
	for (addr = (dir ? req->byte_position : (req->byte_position + len -1));
			(dir && (addr < req->byte_position + len)) || (!dir && (addr > req->byte_position));
			addr = (dir ? (addr + skip) : (addr - skip))) {
		req->core_line_last = ocf_bytes_2_lines(req->cache, dir ? addr : ((addr + 1) - req->byte_length));
		req->core_line_first = req->core_line_last;

		ocf_req_hash(req);
		ocf_engine_lookup(req);
		if (ocf_engine_is_hit(req)) {
			if (first_miss_addr || !dir)
				break;

			/* FORWARD scan + enough HITs: scan backward from the end */
			if (++hits > OCF_MIN(ocf_bytes_2_lines(req->cache, len) / 8, 32)) {
				scanned = (addr + req->byte_length) - req->byte_position;
				if (len > scanned) {
					req->byte_position += scanned;
					return get_pf_req_info(req, len-scanned, 0);
				}
				break;
			}
		} else {
			if (!first_miss_addr)
				first_miss_addr = addr;
			if (++miss > MAX_PF_MB * MiB / ocf_cache_get_line_size(req->cache))
				break;
		}

		left = (dir ? (req->byte_position + len - (addr + req->byte_length)) :
			((addr + 1) - req->byte_position));
		if (left && left < skip) {
			addr = (dir ? (req->byte_position + len - 2 * req->byte_length) :
				(req->byte_position + 2 * req->byte_length -1));
			skip = req->byte_length;
		}
	}

	if (first_miss_addr) {
		if (dir) {
			req->byte_position = first_miss_addr;
			return addr - first_miss_addr;
		} else {
			req->byte_position = addr + 1;
			return first_miss_addr - addr;
		}
	}

	return 0;
}

static int ocf_make_prefetch(struct DasKvParam *param)
{
	struct ocf_request *sizeof_cline_req = NULL;
	struct ocf_io *prefetch_io = NULL;
	uint64_t last_addr, addr;
	uint32_t len = 0, total_len = 0;
	struct ocf_queue *queue = param->queue;
	struct ocf_cache *cache = queue->cache;
	struct ocf_core_volume *core_volume = ocf_volume_get_priv(param->volume);
	struct ocf_core *core = core_volume->core;
	struct ocf_io_internal *ioi;
	struct ocf_request *req;
	bool reject_flag = false;
	void *data;

	/* round address down to whole line size */
	param->offset = ocf_lines_2_bytes(cache, ocf_bytes_2_lines_round_down(cache, param->offset));
	/* round length up to whole line size */
	param->len = ocf_lines_2_bytes(cache, ocf_bytes_2_lines_round_up(cache, param->len));
	/* Abort if request exceeds backend volume size */
	if (unlikely(param->offset >= ocf_volume_get_length(param->volume))) {
		return 0;
	}

	/* Trim the len not to exceed backend volume size */
	param->len = OCF_MIN(param->len, ocf_volume_get_length(param->volume) - param->offset);
	/* Prefetch - Break request accoring to cache lines misses */
	sizeof_cline_req = ocf_req_new(queue, core, param->offset,
		ocf_cache_get_line_size(cache), OCF_READ);
	if(unlikely(sizeof_cline_req == NULL)) {
		ENV_WARN(true, "ocf_req_new(addr = 0x%p, len = 0x%x) failed\n",
			(void *)param->offset, (uint32_t)ocf_cache_get_line_size(cache));
		return 0;
	}

	if ((len = get_pf_req_info(sizeof_cline_req, param->len, 1)) == 0) {
		ocf_req_put(sizeof_cline_req);
		return 0;
	}
	last_addr = sizeof_cline_req->byte_position + param->len;

	for (addr = sizeof_cline_req->byte_position; addr < last_addr; addr += MAX_SINGLE_PF) {
		len = OCF_MIN(MAX_SINGLE_PF, last_addr - addr);
		if (das_is_limit(core)) {
			reject_flag = true;
			env_atomic64_add(_bytes4k(len), &core->counters->das_limit_io_total);
			continue;
		}
		prefetch_io = ocf_volume_new_io(param->volume, queue, addr, len, OCF_READ, 0, 0);
		if (unlikely(prefetch_io == NULL)) {
			ENV_WARN(true, "ocf_volume_new_io(addr = %p, len = 0x%x) failed\n",
				(void *)addr, len);
			break;
		}
		ocf_io_set_cmpl(prefetch_io, NULL, NULL, das_io_cmpl);
		ioi = container_of(prefetch_io, struct ocf_io_internal, io);
		req = container_of(ioi, struct ocf_request, ioi);
		data = ctx_data_alloc(req->cache->owner, BYTES_TO_PAGES(req->byte_length));
		if (unlikely(data == NULL)) {
			ENV_WARN(true, "ctx_data_alloc(%u) failed\n",
				(int)BYTES_TO_PAGES(req->byte_length));
			ocf_io_put(prefetch_io);
			break;
		}
		ocf_io_set_data(prefetch_io, data, 0);
		prefetch_io->is_pf_io = true;
		ocf_core_volume_submit_prefetch(prefetch_io);
	}

	ocf_req_put(sizeof_cline_req);

	if (reject_flag) {
		return RETURN_DAS_REJECT;
	}
	return 0;
}

static int das_log_to_ocf_log_level(enum DasLogLvl level)
{
	switch (level) {
		case DAS_LOGLVL_ERR:
		case DAS_LOGLVL_WAR:
		case DAS_LOGLVL_INF:
			return log_info;
		case DAS_LOGLVL_DBG:
			return log_debug;
		default:
			break;
	}

	return log_info;
}

static void das_log(void *logger, enum DasLogLvl level, const char *format, ...)
{
	ocf_logger_lvl_t retLevel = das_log_to_ocf_log_level(level);
	ocf_log_raw(logger, retLevel, format);
}

void das_init(ocf_cache_t cache)
{
	if (g_ocf_num++ == 0) {
		struct DasModuleParam *initParam = (struct DasModuleParam *)malloc(sizeof(struct DasModuleParam));
		initParam->ops = (struct DasOPS *)malloc(sizeof(struct DasOPS));
		initParam->ops->SubmitDasPrefetch = ocf_make_prefetch;
		initParam->ops->logFunc = das_log;
		initParam->cacheLineSize = cache->conf_meta->line_size;
		initParam->logger = &(ocf_cache_get_ctx(cache)->logger);
		Rcache_CeateDasModule(initParam);

		ocf_cache_log(cache, log_info, "DAS init with cache_line_size %llu kiB\n",
				cache->conf_meta->line_size / KiB);
	}
}

void das_exit(void)
{
	if (--g_ocf_num == 0) {
		Rcache_ExitDasModule();
	}
}

void das_analyze_io(struct ocf_io *io)
{
	struct timespec tn;
	struct DasKvParam param;

	if (unlikely(io == NULL)) {
		ENV_WARN(true, "das analyze io is NULL\n");
		return;
	}

	param.volume = ocf_io_get_volume(io);
	param.queue = io->io_queue;
	param.offset = io->addr;
	param.len = io->bytes;
	clock_gettime(CLOCK_REALTIME, &tn);
	param.timeStamp = tn.tv_sec * OCF_DAS_SEC_TO_NSEC + tn.tv_nsec;

	Rcache_PutDasInfo(&param);
}