/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <syslog.h>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include "check_queue_thread.h"
#include "completion_queue.h"
#include "mngt_queue_thread.h"
#include "queue_thread.h"
#include "ctx.h"
#include "slot_info.h"
#include "log.h"
#include "utils_strbuf.h"
#include "volume.h"
#include "ocf_adaptor.h"
#include "ocf_queue_utils.h"

using namespace std;

#define MAX_CQ_ENTRYS 16

extern "C" ocf_core_id_t ocf_core_get_id(ocf_core_t core);

static const char *STATUS_STR[OCF_STATUS_MAX] = {
	[OCF_STATUS_NONE] = "None",
	[OCF_STATUS_INITIALIZED] = "Initialized",
	[OCF_STATUS_DELETING] = "Deleting",
	[OCF_STATUS_ERROR] = "Error",
};

struct ocf_config g_cfg;

struct ocf_adaptor_context {
	ocf_ctx_t ctx;
	ocf_cache_t cache;

	env_rwlock table_lock = { PTHREAD_RWLOCK_INITIALIZER };
	/* By default, same region or regions in the same slot don't support multi-thread concurrent
	 * operations(that is, multi-thread concurrent operations such as get/put/invalid/lookup)
	 * if it exists, it needs to be locked at the slot granularity
	 */
	unordered_map<uint32_t, slot_info_t> slot_info_table;
	unordered_map<uint32_t, unordered_map<uint32_t, uint32_t>> region_remap_table;
} g_adaptor;

/*
 * Queue ops providing interface for running queue thread in asynchronous
 * way. Optional synchronous kick callback is not provided. The stop()
 * operation is called just before queue is being destroyed.
 */
const struct ocf_queue_ops mqueue_ops = {
	mqueue_thread_kick,
	NULL,
	mqueue_thread_stop,
};

const struct ocf_queue_ops queue_ops = {
	NULL,
	NULL,
	queue_thread_stop,
};

struct cache_priv {
	uint16_t slot_core_id;
	ocf_queue_t mngt_queue;
	ocf_queue_t io_queues[MAX_QUEUE_NUM];
	check_queue_t check_queues[MAX_QUEUE_NUM];
	completion_queue_t completion_queues[MAX_QUEUE_NUM];
	uint32_t queue_num;
};

struct simple_context {
	sem_t sem;
	int *ret;
};

int set_ocf_io_timeout_val(uint64_t val)
{
	set_ocf_check_timeout_val(val);

	return STATE_SUCCESS;
}

static int check_ocf_config(struct ocf_config *cfg)
{
	if (cfg->io_worker_num > MAX_QUEUE_NUM) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_worker_num can not exceed %u\n", MAX_QUEUE_NUM);
		return STATE_FAIL;
	}

	switch (cfg->cache_line_size) {
		case ocf_cache_line_size_8:
		case ocf_cache_line_size_16:
		case ocf_cache_line_size_32:
		case ocf_cache_line_size_64:
			break;
		default:
			ocf_adaptor_log(OCF_LOG_ERROR, "cache line size(%lu) is not suppoerted\n", cfg->cache_line_size);
			return STATE_FAIL;
	}

	if (cfg->cache_capacity > CACHE_MAX_SUPPORT_IN_TIB * TiB) {
		ocf_adaptor_log(OCF_LOG_ERROR, "cache capacity should be <= %d TiB\n", CACHE_MAX_SUPPORT_IN_TIB);
		return STATE_FAIL;
	}

	uint128_t core_mask = cfg->core_mask;
	uint16_t num = 0;
	while (core_mask) {
		core_mask &= (core_mask - 1);
		num++;
	}

	if (num != cfg->core_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "core_num and core_mask do not match\n");
		return STATE_FAIL;
	}
	return STATE_SUCCESS;
}

/*
 * Basic asynchronous completion callback. Just propagate error code and
 * up the semaphore.
 */
static void simple_complete(ocf_cache_t cache, void *priv, int error)
{
	struct simple_context *context= (struct simple_context*)priv;

	*context->ret = error;
	sem_post(&context->sem);
}

static int validate_cache_cfg(struct ocf_config *cfg)
{
	if (cfg->io_worker_num == 0 || cfg->core_num == 0) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_worker_num or core_num is zero, io_worker_num = %u, core_num = %u\n",
			cfg->io_worker_num, cfg->core_num);
		return STATE_FAIL;
	}

	return STATE_SUCCESS;
}

static int initialize_cache(ocf_ctx_t ctx, ocf_cache_t *cache, struct ocf_config *cfg)
{
	struct ocf_mngt_cache_config cache_cfg = { };
	struct ocf_mngt_cache_attach_config attach_cfg = { };
	struct lava_volume_param param;
	ocf_volume_t volume;
	ocf_volume_type_t type;
	struct ocf_volume_uuid uuid;
	struct cache_priv *cache_priv;
	struct simple_context context;
	int stop_ret;
	int ret;
	int i;

	ret = validate_cache_cfg(cfg);
	if (ret)
		return ret;

	/* Initialize completion semaphore */
	ret = sem_init(&context.sem, 0, 0);
	if (ret)
		return ret;

	/*
	 * Asynchronous callbacks will assign error code to ret. That
	 * way we have always the same variable holding last error code.
	 */
	context.ret = &ret;

	/* Cache configuration */
	strcpy(cache_cfg.name, "UCache");
	ocf_mngt_cache_config_set_default(&cache_cfg);
	cache_cfg.metadata_volatile = true;
	cache_cfg.cache_line_size = (ocf_cache_line_size_t)cfg->cache_line_size;

	/* Cache deivce (volume) configuration */
	type = ocf_ctx_get_volume_type(ctx, LAVA_VOL_TYPE);
	ret = ocf_uuid_set_str(&uuid, (char *)"cache");
	if (ret)
		goto err_sem;

	ret = ocf_volume_create(&volume, type, &uuid);
	if (ret)
		goto err_sem;

	param.chunk_num = cfg->cache_capacity / LAVA_CHUNK_SIZE;
	param.chunk_num += ((cfg->cache_capacity % LAVA_CHUNK_SIZE) ? 1 : 0);
	ocf_mngt_cache_attach_config_set_default(&attach_cfg);
	attach_cfg.device.volume = volume;
	attach_cfg.cache_line_size = (ocf_cache_line_size_t)cfg->cache_line_size;
	attach_cfg.device.volume_params = &param;

	/*
	 * Allocate cache private structure. We can not initialize it
	 * on stack, as it may be used in various async contexts
	 * throughout the entire live span of cache object.
	 */
	cache_priv = (struct cache_priv*)malloc(sizeof(*cache_priv));
	if (!cache_priv) {
		ret = -ENOMEM;
		goto err_vol;
	}
	cache_priv->slot_core_id = 0;
	cache_priv->queue_num = cfg->io_worker_num;

	/* Start cache */
	ret = ocf_mngt_cache_start(ctx, cache, &cache_cfg, NULL);
	if (ret) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf mngt cache start failed\n");
		goto err_priv;
	}

	/* Assing cache priv structure to cache. */
	ocf_cache_set_priv(*cache, cache_priv);

	/*
	 * Create management queue. It will be used for performing various
	 * asynchronous management operations, such as attaching cache volume
	 * or adding core object.
	 */
	ret = ocf_queue_create(*cache, &cache_priv->mngt_queue, &mqueue_ops);
	if (ret) {
		ocf_mngt_cache_stop(*cache, simple_complete, &context);
		sem_wait(&context.sem);
		goto err_priv;
	}

	/*
	 * Assign management queue to cache. This has to be done before any
	 * other management operation. Management queue is treated specially,
	 * and it may not be used for submitting IO requests. It also will not
	 * be put on the cache stop - we have to put it manually at the end.
	 */
	ocf_mngt_cache_set_mngt_queue(*cache, cache_priv->mngt_queue);

	/* Create queue which will be used for IO submission. */
	for (i = 0; i < cfg->io_worker_num; ++i) {
		ret = ocf_queue_create(*cache, &cache_priv->io_queues[i], &queue_ops);
		if (ret)
			goto err_cache;
	}

	for (i = 0; i < cfg->io_worker_num; ++i) {
		ret = check_queue_create(*cache, &cache_priv->check_queues[i]);
		if (ret)
			goto err_cache;
	}

	for (i = 0; i < cfg->io_worker_num; ++i) {
		ret = completion_queue_create(&cache_priv->completion_queues[i]);
		if (ret)
			goto err_cache;
	}

	ret = initialize_mngt_threads(cache_priv->mngt_queue);
	if (ret) {
		goto err_cache;
	}

	ret = initialize_threads(cache_priv->io_queues,
		cache_priv->queue_num, cfg->core_num, cfg->core_mask);
	if (ret)
		goto err_cache;

	ret = initialize_check_threads(cache_priv->check_queues,
		cache_priv->queue_num, cfg->core_num, cfg->core_mask);
	if (ret)
		goto err_cache;

	/* Attach volume to cache */
	ocf_mngt_cache_attach(*cache, &attach_cfg, simple_complete, &context);
	sem_wait(&context.sem);
	if (ret) {
		context.ret = &stop_ret;
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf mngt cache attach failed\n");
		goto err_cache;
	}

	return 0;

err_cache:
	ocf_mngt_cache_stop(*cache, simple_complete, &context);
	sem_wait(&context.sem);
	for (uint32_t i = 0; i < cache_priv->queue_num; ++i) {
		check_queue_put(cache_priv->check_queues[i]);
	}
	ocf_queue_put(cache_priv->mngt_queue);
err_priv:
	free(cache_priv);
err_vol:
	ocf_volume_destroy(volume);
err_sem:
	sem_destroy(&context.sem);
	return ret;
}

/*
 * Add core completion callback context. We need this to propagate error code
 * and handle to freshly initialized core object.
 */
struct add_core_context {
	ocf_core_t *core;
	int *error;
	sem_t sem;
};

/* Add core complete callback. Just rewrite args to context structure and
 * up the semaphore.
 */
static void add_core_complete(ocf_cache_t cache, ocf_core_t core,
		void *priv, int error)
{
	struct add_core_context *context = (struct add_core_context*)priv;

	*context->core = core;
	*context->error = error;
	sem_post(&context->sem);
}

static int initialize_core(ocf_cache_t cache, ocf_core_t *core, uint32_t slot_id)
{
	struct ocf_mngt_core_config core_cfg = { };
	struct add_core_context context;
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(cache);
	int ret;

	/* Initialize completion semaphore */
	ret = sem_init(&context.sem, 0, 0);
	if (ret)
		return ret;

	/*
	 * Asynchronous callback will assign core handle to core,
	 * and to error code to ret.
	 */
	context.core = core;
	context.error = &ret;

	/* Core configuration */
	ocf_mngt_core_config_set_default(&core_cfg);
	sprintf(core_cfg.name, "slot%u-%u", slot_id, ++(priv->slot_core_id));
	core_cfg.volume_type = CORE_VOL_TYPE;
	ret = ocf_uuid_set_str(&core_cfg.uuid, core_cfg.name);
	if (ret)
		goto err_sem;

	/* Add core to cache */
	ocf_mngt_cache_add_core(cache, &core_cfg, add_core_complete, &context);
	sem_wait(&context.sem);

	if (priv->slot_core_id == OCF_CORE_MAX)
		priv->slot_core_id = 0;

err_sem:
	sem_destroy(&context.sem);

	return ret;
}

static void region_remove_complete(ocf_cache_t cache, void *priv, int error)
{
	// region_remove启动后一定会成功，可以不设置回调，后续有回调需求可以实现
}

static void single_core_remove_complete(void *ctx, int ret)
{
	// region_remove启动后一定会成功，可以不设置回调，后续有回调需求可以实现
}

static void core_remove_complete(void *ctx, int ret)
{
	struct simple_context *context = (struct simple_context *)ctx;

	if (ret) {
		*context->ret = ret;
	}
	sem_post(&context->sem);
}

static void cache_remove_complete(ocf_cache_t cache, void *ctx, int ret)
{
	struct simple_context *context = (struct simple_context *)ctx;

	if (ret) {
		*context->ret = ret;
	}
	sem_post(&context->sem);
}

static void complete(struct ocf_io *io, int error)
{
	struct req_context *ctx = (struct req_context *)io->priv1;

	ocf_cache_t cache = ocf_queue_get_cache(io->io_queue);
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(cache);
	completion_queue_t cq = priv->completion_queues[ctx->io_worker_id];

	cq_entry_t entry = (cq_entry_t)ctx->internal;
	int ret;
	int op = io->dir;
	switch (error) {
		case 0:
			ret = STATE_SUCCESS;
			break;
		case -OCF_ERR_TIMEOUT_IO:
			ret = STATE_CHUNK_TIMEOUT;
			break;
		default:
			ret = ((op == OCF_LOOKUP || op == OCF_READ) ? STATE_MISS : STATE_FAIL);
			break;
	}

	entry->ret = ret;
	completion_queue_push(cq, entry);

	ocf_io_put(io);
}

static int submit_io(struct req_context *ctx, ocf_core_t core,
	uint64_t addr, uint64_t len, int dir, ocf_end_io_t cmpl)
{
	ocf_cache_t cache = ocf_core_get_cache(core);
	ocf_volume_t core_vol = ocf_core_get_front_volume(core);
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(cache);
	if (unlikely(ctx->io_worker_id >= priv->queue_num)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_work_id(%u) is not within the range of [0, %u)\n",
			ctx->io_worker_id, priv->queue_num);
		return STATE_PARAM_INVALID;
	}

	ocf_queue_t q = priv->io_queues[ctx->io_worker_id];
	/* allocate new io */
	struct ocf_io *io = ocf_volume_new_io(core_vol, q, addr, len, dir, 0, 0);
	if (unlikely(!io)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io memory request fail\n");
		return STATE_MEM_ALLOC_ERR;
	}

	struct volume_data *data = (struct volume_data *)(ctx->internal + sizeof(struct cq_entry));
	data->ptr = ctx->buffer;
	data->offset = 0;
	/* assign data to io, used when read/write, unused when lookup/invalid */
	ocf_io_set_data(io, data, 0);
	/* setup completion function */
	ocf_io_set_cmpl(io, ctx, NULL, cmpl);

	struct ocf_request *req = ocf_io_to_req(io);
	check_queue_push(priv->check_queues[ctx->io_worker_id], req);
	/* submit io */
	ocf_core_submit_io(io);

	return STATE_SUCCESS;
}

int ocf_init(struct ocf_config *cfg)
{
	g_cfg = *cfg;

	ocf_update_metadata_cfg(cfg->cache_line_size);
	
	if (get_ocf_global_status() != OCF_STATUS_NONE) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf has been initialized\n");
		return STATE_FAIL;
	}

	if (!cfg) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_init cfg is NULL\n");
		return STATE_PARAM_INVALID;
	}

	if (cfg->log_print) {
		set_log_print(cfg->log_print);
	}

	if (check_ocf_config(cfg)) {
		return STATE_PARAM_INVALID;
	}

	if (ctx_init(&g_adaptor.ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf ctx init failed\n");
		return STATE_FAIL;
	}

	if (initialize_cache(g_adaptor.ctx, &g_adaptor.cache, cfg)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf cache init failed\n");
		ctx_cleanup(g_adaptor.ctx);
		return STATE_FAIL;
	}

	set_ocf_global_status(OCF_STATUS_INITIALIZED);
	ocf_adaptor_log(OCF_LOG_INFO, "ocf init complete\n");
	return STATE_SUCCESS;
}

void ocf_exit()
{
	if (get_ocf_global_status() != OCF_STATUS_INITIALIZED && get_ocf_global_status() != OCF_STATUS_ERROR) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf is not initialized or error, not need to delete\n");
		return;
	}

	ocf_adaptor_log(OCF_LOG_INFO, "ocf exiting, wait for a while\n");
	env_msleep(500);

	set_ocf_global_status(OCF_STATUS_DELETING);

	int ret = STATE_SUCCESS;
	simple_context ctx;
	ctx.ret = &ret;
	sem_init(&ctx.sem, 0, 0);
	unordered_map<uint32_t, slot_info_t> slot_info_table;
	unordered_map<uint32_t, unordered_map<uint32_t, uint32_t>> region_remap_table;

	/* clear slot hash table */
	env_rwlock_write_lock(&g_adaptor.table_lock);
	swap(slot_info_table, g_adaptor.slot_info_table);
	swap(region_remap_table, g_adaptor.region_remap_table);

	/* Remove core from cache */
	for (auto it: slot_info_table) {
		slot_info_t info = it.second;
		ret = ocf_mngt_remove_core(info->core, core_remove_complete, &ctx);
		if (ret) {
			core_remove_complete(&ctx, ret);
		}
		env_free(info);
	}
	for (uint32_t i = 0; i < slot_info_table.size(); ++i) {
		sem_wait(&ctx.sem);
	}

	env_rwlock_write_unlock(&g_adaptor.table_lock);

	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf core remove fail when exiting(%d)\n", ret);
	}

	/* Stop cache */
	ret = STATE_SUCCESS;
	ocf_mngt_cache_stop(g_adaptor.cache, cache_remove_complete, &ctx);
	sem_wait(&ctx.sem);
	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_WARN, "ocf cache remove fail(%d)\n", ret);
	}

	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(g_adaptor.cache);

	/* Put the management queue */
	ocf_queue_put(priv->mngt_queue);

	for (uint32_t i = 0; i < priv->queue_num; ++i) {
		completion_queue_put(priv->completion_queues[i], 1);
	}

	for (uint32_t i = 0; i < priv->queue_num; ++i) {
		check_queue_put(priv->check_queues[i]);
	}

	free(priv);

	/* Deinitialize context */
	ctx_cleanup(g_adaptor.ctx);

	/* Destroy completion semaphore */
	sem_destroy(&ctx.sem);

	set_ocf_global_status(OCF_STATUS_NONE);
	ocf_adaptor_log(OCF_LOG_INFO, "ocf exit complete\n");
}

int ocf_add_core(uint32_t slot_id)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not add core\n");
		return STATE_OCF_UNAVAILABLE;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	auto &table_lock = g_adaptor.table_lock;
	slot_info_t info;
	env_rwlock_write_lock(&table_lock);
	if (slot_info_table.find(slot_id) != slot_info_table.end()) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core already exists\n", slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_CORE_EXIST;
	} else {
		info = (slot_info_t)env_zalloc(sizeof(*info), 0);
		if (!info) {
			env_rwlock_write_unlock(&table_lock);
			return STATE_MEM_ALLOC_ERR;
		}
		slot_info_table[slot_id] = info;
	}
	env_rwlock_write_unlock(&table_lock);

	ocf_core_t core;
	if (initialize_core(g_adaptor.cache, &core, slot_id)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core init failed\n", slot_id);
		env_rwlock_write_lock(&table_lock);
		slot_info_table.erase(slot_id);
		env_rwlock_write_unlock(&table_lock);
		env_free(info);
		return STATE_FAIL;
	}

	env_rwlock_write_lock(&table_lock);
	info->core = core;
	region_remap_table[slot_id] = unordered_map<uint32_t, uint32_t>();
	env_rwlock_write_unlock(&table_lock);
	ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) core(%u) add success\n", slot_id, ocf_core_get_id(core));
	return STATE_SUCCESS;
}

int ocf_remove_core(uint32_t slot_id)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not remove core\n");
		return STATE_OCF_UNAVAILABLE;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	auto &table_lock = g_adaptor.table_lock;
	slot_info_t info;
	ocf_core_t core;
	ocf_core_id_t core_id;
	env_rwlock_write_lock(&table_lock);
	if (slot_info_table.find(slot_id) == slot_info_table.end()) {
		ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) core does not exist\n", slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_CORE_NOT_EXIST;
	}
	info = slot_info_table[slot_id];
	if (!info->core) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core is creating, can not remove\n", slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_CORE_CREATING;
	}

	/* remove core from cache */
	core = info->core;
	core_id = ocf_core_get_id(core);

	int ret = 0;

	ret = ocf_mngt_remove_core(core, single_core_remove_complete, NULL);

	if (ret) {
		env_rwlock_write_unlock(&table_lock);
		return STATE_MEM_ALLOC_ERR;
	}

	/* erase slot id */
	slot_info_table.erase(slot_id);
	region_remap_table.erase(slot_id);
	ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) core(%u) remove success\n", slot_id, core_id);
	env_rwlock_write_unlock(&table_lock);

	env_free(info);
	return STATE_SUCCESS;
}

int ocf_remove_region(uint32_t slot_id, uint32_t region_id)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not submit region_invalid io\n");
		return STATE_OCF_UNAVAILABLE;
	}

	/* find the remap id for the region */
	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_write_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(slot_id) == region_remap_table.end()) {
		env_rwlock_write_unlock(&g_adaptor.table_lock);
		ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) core does not exist\n", slot_id);
		return STATE_CORE_NOT_EXIST;
	}

	slot_info_t info = slot_info_table[slot_id];
	auto &region_map = region_remap_table[slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(region_id) == region_map.end()) {
		env_rwlock_write_unlock(&g_adaptor.table_lock);
		ocf_adaptor_log(OCF_LOG_INFO, "region(%u) does not exist\n", region_id);
		return STATE_REGION_NOT_EXIST;
	}
	uint64_t remap_id = region_map[region_id];
	int ret = ocf_mngt_cache_remove_corelines(core, remap_id * REGION_SIZE, REGION_SIZE,
		region_remove_complete, NULL);
	if (ret) {
		env_rwlock_write_unlock(&g_adaptor.table_lock);
		return STATE_MEM_ALLOC_ERR;
	}

	region_map.erase(region_id);
	env_rwlock_write_unlock(&g_adaptor.table_lock);

	ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) remove region_id(%u)-remap_id(%lu)\n", slot_id, region_id, remap_id);
	return STATE_SUCCESS;
}

int ocf_invalid(struct req_context *ctx)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf is not initialized, can not submit range_invalid io\n");
		return STATE_OCF_UNAVAILABLE;
	}

	if (unlikely(!ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_invalid ctx is NULL\n");
		return STATE_PARAM_INVALID;
	}

	/* find the remap id for the region */
	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(ctx->slot_id) == region_remap_table.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_SUCCESS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	/* align left and right, calculate the actual offset on the core */
	uint64_t left_pad = ctx->offset % ALIGN_SIZE;
	uint64_t right_pad = (ALIGN_SIZE - ((ctx->offset + ctx->len) % ALIGN_SIZE)) % ALIGN_SIZE;
	uint64_t offset = ctx->offset - left_pad;
	uint64_t len = ctx->len + (left_pad + right_pad);
	uint64_t core_offset = remap_id * REGION_SIZE + offset;
	return submit_io(ctx, core, core_offset, len, OCF_INVALID, complete);
}

int ocf_lookup(struct req_context *ctx)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf is not initialized, can not submit lookup io\n");
		return STATE_OCF_UNAVAILABLE;
	}

	if (unlikely(!ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_lookup ctx is NULL\n");
		return STATE_PARAM_INVALID;
	}

	if ((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ock_lookup is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}

	/* find the remap id for the region */
	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (unlikely(region_remap_table.find(ctx->slot_id) == region_remap_table.end())) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	/* align left and right, calculate the actual offset on the core */
	uint64_t left_pad = ctx->offset % ALIGN_SIZE;
	uint64_t right_pad = (ALIGN_SIZE - ((ctx->offset + ctx->len) % ALIGN_SIZE)) % ALIGN_SIZE;
	uint64_t offset = ctx->offset - left_pad;
	uint64_t len = ctx->len + (left_pad + right_pad);
	uint64_t core_offset = remap_id * REGION_SIZE + offset;
	return submit_io(ctx, core, core_offset, len, OCF_LOOKUP, complete);
}

int ocf_get(struct req_context *ctx)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf is not initialized, can not submit read io\n");
		return STATE_OCF_UNAVAILABLE;
	}

	if (unlikely(!ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_get ctx is NULL\n");
		return STATE_PARAM_INVALID;
	}

	if ((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf_get is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}

	/* find the remap id for the region */
	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (unlikely(region_remap_table.find(ctx->slot_id) == region_remap_table.end())) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	/* calculate the actual offset on the core */
	uint64_t core_offset = remap_id * REGION_SIZE + ctx->offset;
	return submit_io(ctx, core, core_offset, ctx->len, OCF_READ, complete);
}

int ocf_put(struct req_context *ctx)
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf is not initialized, can not submit write io\n");
		return STATE_OCF_UNAVAILABLE;
	}

	if (unlikely(!ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_put ctx is NULL\n");
		return STATE_PARAM_INVALID;
	}

	if (unlikely((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE))) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf_put is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_SUCCESS, ctx);
		}
		return STATE_SUCCESS;
	}

	/* find the remap id for the region */
	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_write_lock(&g_adaptor.table_lock);
	if (unlikely(region_remap_table.find(ctx->slot_id) == region_remap_table.end())) {
		env_rwlock_write_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	slot_info_t info = slot_info_table[ctx->slot_id];
	ocf_core_t core = info->core;
	auto &region_map = region_remap_table[ctx->slot_id];
	int remap_id;
	if (region_map.find(ctx->region_id) != region_map.end()) {
		remap_id = region_map[ctx->region_id];
	} else {
		remap_id = get_remap_id(info);
		if (remap_id < 0) {
			env_rwlock_write_unlock(&g_adaptor.table_lock);
			if (ctx->cb) {
				ctx->cb(STATE_TOO_MANY_REGION, ctx);
			}
			return STATE_SUCCESS;
		}
		region_map[ctx->region_id] = remap_id;
		ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) add region_id(%u)-remap_id(%d)\n",
			ctx->slot_id, ctx->region_id, remap_id);
	}
	env_rwlock_write_unlock(&g_adaptor.table_lock);

	/* calculate the actual offset on the core */
	uint64_t core_offset = remap_id * REGION_SIZE + ctx->offset;
	return submit_io(ctx, core, core_offset, ctx->len, OCF_WRITE, complete);
}

int ocf_poll(uint32_t io_worker_id, int max_num)
{
	if (unlikely((get_ocf_global_status() != OCF_STATUS_INITIALIZED) && 
			(get_ocf_global_status() != OCF_STATUS_ERROR))) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "ocf is not working, can not poll\n");
		return STATE_OCF_UNAVAILABLE;
	}

	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(g_adaptor.cache);
	if (unlikely(io_worker_id >= priv->queue_num)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_work_id(%u) can not exceed %u\n", io_worker_id, priv->queue_num);
		return STATE_PARAM_INVALID;
	}

	int limit;
	if (!max_num) {
		limit = MAX_CQ_ENTRYS;
	} else {
		limit = (max_num > MAX_CQ_ENTRYS) ? MAX_CQ_ENTRYS : max_num;
	}

	completion_queue_t q = priv->completion_queues[io_worker_id];
	cq_entry_t entrys[MAX_CQ_ENTRYS];
	cq_entry_t entry;
	int num = completion_queue_pop(q, entrys, limit);
	struct req_context *ctx;
	for (int i = 0; i < num; ++i) {
		entry = entrys[i];
		ctx = (struct req_context *)get_req_context(entry);
		ctx->cb(entry->ret, ctx);
	}
	return STATE_SUCCESS;
}

struct ocf_dump_info *ocf_dump_cache_core_info()
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not dump info\n");
		return NULL;
	}

	struct ocf_dump_info *info = (struct ocf_dump_info *)env_zalloc(sizeof(struct ocf_dump_info) + sizeof(void *), 0);
	if (!info) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info memory malloc fail\n");
		return NULL;
	}

	struct strbuf *b = ocf_ctx_dump_cache_core_info(g_adaptor.ctx, ocf_cache_get_name(g_adaptor.cache));
	if (!b) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info get fail\n");
		ocf_release_dump_info(info);
		return NULL;
	}

	info->buf = b->buf;
	info->len = b->cur;
	struct strbuf **tail = (struct strbuf **)((char *)info + sizeof(struct ocf_dump_info));
	*tail = b;

	return info;
}

struct node {
	uint32_t slot_id;
	uint32_t region_id;
	uint32_t remap_id;
	bool operator <(const node &r) {
		if (slot_id != r.slot_id) {
			return slot_id < r.slot_id;
		}

		return region_id < r.region_id;
	}
};

struct ocf_dump_info *ocf_dump_region_info()
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not dump region info\n");
		return NULL;
	}

	struct ocf_dump_info *info = (struct ocf_dump_info *)env_zalloc(sizeof(struct ocf_dump_info) + sizeof(void *), 0);
	if (!info) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info memory malloc fail\n");
		return NULL;
	}

	struct strbuf *b = new_strbuf();
	if (!b) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump strbuf memory malloc fail\n");
		env_free(info);
		return NULL;
	}

	unordered_map<uint32_t, unordered_map<uint32_t, uint32_t>> region_remap_table;
	env_rwlock_write_lock(&g_adaptor.table_lock);
	region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_write_unlock(&g_adaptor.table_lock);

	vector<node> v;
	for (auto &it: region_remap_table) {
		uint32_t slot_id = it.first;
		for (auto &kv: it.second) {
			v.push_back({slot_id, kv.first, kv.second});
		}
	}
	sort(v.begin(), v.end());

	strbuf_write_str(b, "<slot_id>   <region_id>   <remap_id>\n");
	for (auto &x: v) {
		strbuf_write_format_str(b, "%-12u%-14u%u\n", x.slot_id, x.region_id, x.remap_id);
	}
	strbuf_write_char(b, '\n');

	info->buf = b->buf;
	info->len = b->cur;
	struct strbuf **tail = (struct strbuf **)((char *)info + sizeof(struct ocf_dump_info));
	*tail = b;

	return info;
}

struct ocf_dump_info *ocf_dump_cache_stats()
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not dump cache stats\n");
		return NULL;
	}

	struct ocf_dump_info *info = (struct ocf_dump_info *)env_zalloc(sizeof(struct ocf_dump_info) + sizeof(void *), 0);
	if (!info) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info memory malloc fail\n");
		return NULL;
	}

	struct strbuf *b = ocf_stats_dump_cache(g_adaptor.ctx, ocf_cache_get_name(g_adaptor.cache));
	if (!b) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info get fail\n");
		ocf_release_dump_info(info);
		return NULL;
	}

	info->buf = b->buf;
	info->len = b->cur;
	struct strbuf **tail = (struct strbuf **)((char *)info + sizeof(struct ocf_dump_info));
	*tail = b;

	return info;
}

struct ocf_dump_info *ocf_dump_status()
{
	struct ocf_dump_info *info = (struct ocf_dump_info *)env_zalloc(sizeof(struct ocf_dump_info) + sizeof(void *), 0);
	if (!info) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info memory malloc fail\n");
		return NULL;
	}

	struct strbuf *b = new_strbuf();
	if (!b) {
		ocf_adaptor_log(OCF_LOG_ERROR, "dump info memory malloc fail\n");
		return NULL;
	}

	int state = get_ocf_global_status();
	if (state >= OCF_STATUS_MAX) {
		state = OCF_STATUS_NONE;
	}

	strbuf_write_format_str(b, "OCF status: %s\n", STATUS_STR[state]);

	info->buf = b->buf;
	info->len = b->cur;
	struct strbuf **tail = (struct strbuf **)((char *)info + sizeof(struct ocf_dump_info));
	*tail = b;

	return info;
}

int ocf_reset_cache_stats()
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not dump cache stats\n");
		return -1;
	}

	return ocf_stats_reset_cache(g_adaptor.ctx, ocf_cache_get_name(g_adaptor.cache));
}

int ocf_reset_lattency_stats()
{
	if (unlikely(get_ocf_global_status() != OCF_STATUS_INITIALIZED)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not dump cache stats\n");
		return -1;
	}

	return ocf_stats_reset_lattency(g_adaptor.ctx, ocf_cache_get_name(g_adaptor.cache));
}

void ocf_release_dump_info(struct ocf_dump_info *info)
{
	if (info) {
		struct strbuf **tail = (struct strbuf **)((char *)info + sizeof(struct ocf_dump_info));
		delete_strbuf(*tail);
		env_free(info);
	}
}

void ocf_show_alock(){
	ocf_cache_t cache = g_adaptor.cache;
	ocf_check_metadata_alock(cache);
}

int ocf_recovery(struct ocf_config *cfg)
{
	if (get_ocf_global_status() != OCF_STATUS_NONE && get_ocf_global_status() != OCF_STATUS_ERROR) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf is not error or none, not need recovery\n");
		return STATE_FAIL;
	}

	ocf_exit();
	int ret = ocf_init(cfg);

	return ret;
}
