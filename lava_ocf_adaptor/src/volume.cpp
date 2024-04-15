/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ocf/ocf.h>
#include "ocf/ocf_cache.h"
#include "ocf_queue_utils.h"
#include "ocf_space.h"
#include "ocf_stats_priv.h"
#include "utils_request.h"
#include "utils_refcnt.h"
#include "ocf_request.h"
#include <algorithm>
#include <vector>
#include <functional>
#include <numeric>
#include "device.h"
#include "volume.h"
#include "ctx.h"
#include "log.h"

using namespace std;

struct lava_volume {
	const char *name;
	std::vector<uint64_t> *chunk_ids;
	std::vector<uint8_t> *chunk_status;
};

struct no_io_volume {
	uint64_t cache_line_size;
};

static int alloc_chunks(std::size_t num, std::vector<uint64_t> *chunk_ids)
{
	int ret, _try = ALLOC_CHUNK_RETRY;
	while(_try--) {
		ret = AllocChunks(num, chunk_ids);
		if (ret) {
			ocf_adaptor_log(OCF_LOG_ERROR, "Alloc chunk faied, maybe network is disconnect\n");
			env_msleep(100);
		} else {
			break;
		}
	}

	return ret;
}

/*
 * In open() function we store uuid data as volume name (for debug messages)
 * and allocate chunk to excute IO operation.
 */
static int lava_volume_open(ocf_volume_t volume, void *volume_params)
{
	int ret;
	const struct ocf_volume_uuid *uuid = ocf_volume_get_uuid(volume);
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);
	struct lava_volume_param *param = (struct lava_volume_param*)volume_params;

	lava_volume->name = ocf_uuid_to_str(uuid);
	lava_volume->chunk_ids = new std::vector<uint64_t>();
	lava_volume->chunk_status = new std::vector<uint8_t>();
	ret = alloc_chunks(param->chunk_num, lava_volume->chunk_ids);
	lava_volume->chunk_status->resize(param->chunk_num, CHUNK_STATUS_VALID);

	if (ret) {
		ocf_adaptor_log(OCF_LOG_ERROR, "Lava chunk open faied with ret:%d\n", ret);
		return -1;
	}

	ocf_adaptor_log(OCF_LOG_INFO, "VOL OPEN: (name: %s)\n", lava_volume->name);

	return 0;
}

/*
 * In close() function we just free chunk allocated in open().
 */
static void lava_volume_close(ocf_volume_t volume)
{
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);

	FreeChunks(lava_volume->chunk_ids);
	delete lava_volume->chunk_ids;
	delete lava_volume->chunk_status;

	ocf_adaptor_log(OCF_LOG_INFO, "VOL CLOSE: (name: %s)\n", lava_volume->name);
}


static void lava_volume_recovery_one_chunk_cb(ocf_cache_t cache, void *context, int chunk_id)
{
	int ret;
	struct lava_volume *lava_volume = (struct lava_volume*)context;

	std::vector<uint64_t> new_avail_chunk_ids;
	ret = alloc_chunks(1, &new_avail_chunk_ids);

	if (unlikely(ret)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "Alloc chunk failed for chunk %d",
			(*lava_volume->chunk_ids)[chunk_id]);
		(*lava_volume->chunk_status)[chunk_id] |= CHUNK_STATUS_DELET_FAIL;

		set_ocf_global_status(OCF_STATUS_ERROR);

		return;
	}

	ocf_adaptor_log(OCF_LOG_INFO, "Recoveried one chunk from %d to %d",
		(*lava_volume->chunk_ids)[chunk_id], new_avail_chunk_ids[0]);

	(*lava_volume->chunk_ids)[chunk_id] = new_avail_chunk_ids[0];
	(*lava_volume->chunk_status)[chunk_id] = CHUNK_STATUS_VALID;
}

static void lava_volume_recovery_one_chunk(ocf_cache_t cache, struct lava_volume *lava_volume, uint32_t bad_chunk_idx)
{
	int ret;

	if ((*lava_volume->chunk_status)[bad_chunk_idx] & CHUNK_STATUS_DELETING) {
		return;
	}

	ocf_adaptor_log(OCF_LOG_ERROR, "Start recovery chunk %d", (*lava_volume->chunk_ids)[bad_chunk_idx]);

	(*lava_volume->chunk_status)[bad_chunk_idx] |= CHUNK_STATUS_DELETING;
	ret = ocf_mngt_cache_remove_cachelines(cache, bad_chunk_idx * LAVA_CHUNK_SIZE, LAVA_CHUNK_SIZE,
		lava_volume_recovery_one_chunk_cb, lava_volume, bad_chunk_idx);

	if (ret) {
		/* set valid, wait for next process */
		(*lava_volume->chunk_status)[bad_chunk_idx] = CHUNK_STATUS_VALID;

		ocf_adaptor_log(OCF_LOG_ERROR, "Recovery chunk failed on %d", (*lava_volume->chunk_ids)[bad_chunk_idx]);
	}
}


static void lava_volume_submit_io_cb(int ret, void *context)
{
	Request *req = (Request*)context;
	struct ocf_io *io = (struct ocf_io*)req->user_ctx;
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);

	if (unlikely(ret)) {
		lava_volume_io->ret = ret;
	}

	delete req;
	if (env_atomic_dec_return(&lava_volume_io->req_cnt) == 0) {
		io->end(io, lava_volume_io->ret);
 	}
}

/*
 * In submit_io() function use chunk API to finish IO operation.
 */
static void lava_volume_submit_io(struct ocf_io *io)
{
	int ret;
	uint64_t addr = io->addr;
	uint32_t io_length = io->bytes;
	uint32_t submitted_len = 0;
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);
	struct volume_data *data;
	struct lava_volume *lava_volume;

	data = (struct volume_data*)ocf_io_get_data(io);
	lava_volume = (struct lava_volume*)ocf_volume_get_priv(ocf_io_get_volume(io));

	env_atomic_set(&lava_volume_io->req_cnt, 1);
	while (io_length > 0) {
		Segment s;
		Request *req = new Request();
		uint32_t chunk_remain = LAVA_CHUNK_SIZE - ((addr + submitted_len) % LAVA_CHUNK_SIZE);
		s.offset = (addr + submitted_len) % LAVA_CHUNK_SIZE;
		if (chunk_remain > io_length) {
			s.length = io_length;
			io_length = 0;
		} else {
			s.length = chunk_remain;
			io_length -= chunk_remain;
		}
		s.data = data->ptr + lava_volume_io->offset + submitted_len;
		req->chunk_id = (*lava_volume->chunk_ids)[((addr + submitted_len) / LAVA_CHUNK_SIZE)];
		req->segments.push_back(s);
		req->user_ctx = io;
		req->cb = lava_volume_submit_io_cb;
		submitted_len += s.length;
		env_atomic_inc(&lava_volume_io->req_cnt);

		if ((*lava_volume->chunk_status)[io->addr / LAVA_CHUNK_SIZE] != CHUNK_STATUS_VALID) {
			lava_volume_submit_io_cb(-OCF_ERR_UCACHE_IO, req);
			continue;
		}

		if (io->dir == OCF_WRITE) {
			ret = AioWrite(req);
		} else {
			ret = AioRead(req);
		}

		if (ret) {
			ocf_adaptor_log(OCF_LOG_ERROR, "Chunk %d IO failed with ret:%d", req->chunk_id, ret);
			lava_volume_submit_io_cb(ret, req);
		}
	}

	if (env_atomic_dec_return(&lava_volume_io->req_cnt) == 0) {
		io->end(io, lava_volume_io->ret);
	}

	ocf_adaptor_log(OCF_LOG_DEBUG, "VOL: (name: %s), IO: (dir: %s, addr: %ld, bytes: %d)\n",
			lava_volume->name, io->dir == OCF_READ ? "read" : "write",
			io->addr, io->bytes);
}

static void _ocf_engine_update_latency_stats_int_adaptor(struct ocf_request *req, int c)
{
	uint64_t start_timestamp, end_timestamp;
	switch (c) {
		case STATS_CLASS_OCF:
			start_timestamp = req->ocf_start_timestamp;
			break;
		case STATS_CLASS_BACKEND:
			start_timestamp = req->backend_start_timestamp;
			break;
		default:
			ENV_BUG();
	}

	/* return if start timestamp is zero */
	if (start_timestamp == 0) {
		return;
	}

	/*end timestamp */
	end_timestamp = env_get_tick_count();

	if (start_timestamp > end_timestamp) {
		/* clock drift may cause this situation */
		ocf_adaptor_log(OCF_LOG_ERROR, "Timestamp error start: %lu, end: %lu\n",
				start_timestamp, end_timestamp);
		return;
	}

	ocf_core_stats_latency_update(req->core, req->part_id,
			c, ocf_req_get_stats_type(req), end_timestamp-start_timestamp);
}

static void lava_volume_submit_req_cb(int ret, void *context)
{
	Request *chunk_req = (Request*)context;
	struct ocf_request *ocf_req = (struct ocf_request*)chunk_req->user_ctx;
	ocf_req_end_t callback = (ocf_req_end_t)ocf_req->backend_complete;
	ocf_volume_t volume = ocf_cache_get_volume(ocf_req->cache);

	delete chunk_req;

	/* calc backend latency */
	_ocf_engine_update_latency_stats_int_adaptor(ocf_req, STATS_CLASS_BACKEND);
	ocf_refcnt_dec(&volume->refcnt);
	callback(ocf_req, ret);
}

static void _get_offset_dict(vector<uint64_t> &offset_dict,
		struct ocf_request *req, uint64_t cacheline_size)
{
	uint64_t seek, total_bytes = 0;

	/* first coreline does not need to be shifted by offset */
	offset_dict.push_back(0);

	seek = req->byte_position % cacheline_size;
	total_bytes += (cacheline_size - seek);

	for (uint32_t i = 1; i < req->core_line_count; i++) {
		offset_dict.push_back(total_bytes);
		total_bytes += cacheline_size;
	}
}

static void _do_send_chunk_request(ocf_request *ocf_req, Request *chunk_req, int dir, uint64_t blocksize)
{
	int ret;
	ocf_volume_t volume = ocf_cache_get_volume(ocf_req->cache);
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);
	uint64_t chunk_idx = chunk_req->chunk_id;

	/* all segments are treated as one request, so we increment the conter here */
	env_atomic_add(1, &ocf_req->req_remaining);
	/* add ref cnt of cache volume */
	if (!ocf_refcnt_inc(&volume->refcnt)) {
		delete chunk_req;
		/* directly callback */
		ocf_req_end_t callback = (ocf_req_end_t)ocf_req->backend_complete;
		callback(ocf_req, -OCF_ERR_NO_MEM);
		return;
	}

	if (!volume->opened) {
		ret = -OCF_ERR_IO;
		goto done;
	}

	if ((*lava_volume->chunk_status)[chunk_idx] != CHUNK_STATUS_VALID) {
		lava_volume_submit_req_cb(-OCF_ERR_UCACHE_IO, chunk_req);
		return;
	}

	/* transfer chunk_id to true chunk_id backend */
	chunk_req->chunk_id = (*lava_volume->chunk_ids)[chunk_idx];

	if (dir == OCF_WRITE) {
		ret = AioWrite(chunk_req);
	} else {
		ret = AioRead(chunk_req);
	}

	/* update block stats */
	ocf_core_stats_cache_block_update(ocf_req->core, ocf_req->ioi.io.io_class,
			dir, blocksize);

done:
	if (ret) {
		ocf_adaptor_log(OCF_LOG_DEBUG, "Chunk IO failed with ret: %d\n", ret);

		if (ret == CHUNK_NOT_AVAIL) {
			/* redirect lava err to ocf internal err */
			ret = -OCF_ERR_UCACHE_CHUNK_NOT_AVAIL;

			(*lava_volume->chunk_status)[chunk_idx] |= CHUNK_STATUS_INVALID;
			lava_volume_recovery_one_chunk(ocf_req->cache, lava_volume, chunk_idx);
		}

		lava_volume_submit_req_cb(ret, chunk_req);
	}
}

static void lava_volume_submit_dummy_io_cb(int ret, void *context)
{
	Request *req = (Request*)context;
	free(req->segments[0].data);
	delete req;
}

static void lava_volume_submit_dummy_io(ocf_volume_t volume, uint32_t period)
{
	static uint32_t prev_time = env_ticks_to_secs(env_get_tick_count());
	static uint8_t is_submitting_io = false;

	if (is_submitting_io) {
		return;
	}

	is_submitting_io = true;

	uint32_t now_time = env_ticks_to_secs(env_get_tick_count());
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);

	uint32_t time_hash_sidx = prev_time % period;
	uint32_t time_hash_eidx = now_time % period;
	uint64_t i, total_io = lava_volume->chunk_ids->size();

	for (i = total_io * time_hash_sidx / period; i != total_io * time_hash_eidx / period;) {
		if (likely((*lava_volume->chunk_status)[i] == CHUNK_STATUS_VALID)) {
			Request *req = new Request();
			Segment s = {
				.offset = 0,
				.length = 4 * KiB,
				.data = (char*)malloc(4 * KiB)
			};
			if (!s.data) {
				delete req;
				is_submitting_io = false;
				return;
			}

			req->chunk_id = (*lava_volume->chunk_ids)[i];
			req->segments.push_back(s);
			req->user_ctx = lava_volume;
			req->cb = lava_volume_submit_dummy_io_cb;

			if (unlikely(AioRead(req) == CHUNK_NOT_AVAIL)) {
				(*lava_volume->chunk_status)[i] |= CHUNK_STATUS_INVALID;
				lava_volume_recovery_one_chunk(volume->cache, lava_volume, i);
				ocf_adaptor_log(OCF_LOG_ERROR, "Dummy IO discovery bad chunk %d, auto recovery \n",
						(*lava_volume->chunk_ids)[i]);
				free(s.data);
				delete req;
			}
		}
		i++;
		i %= total_io;
	}

	prev_time = now_time;
	is_submitting_io = false;
}

static void lava_volume_submit_req(uint64_t cacheline_size,
		uint64_t metadata_offset, void *req_p, int dir, void *callback_p)
{
	struct ocf_request *req = (struct ocf_request*)req_p;
	ocf_req_end_t callback = (ocf_req_end_t)callback_p;
	struct volume_data *data = NULL;
	Request *chunk_req = NULL;
	vector<uint64_t> offset_dict;

	uint64_t addr, bytes, pre_chunk_id, chunk_id, blocksize = 0, total_bytes = 0;
	uint32_t size, cur = 0;

	/*
	 * get offset dict. This means that different corelines correspond
	 * to different start positions of the data pointer in the request
	 */
	_get_offset_dict(offset_dict, req, cacheline_size);
	size = req->byte_length;
	data = (struct volume_data*)req->data;
	req->backend_complete = callback;
	pre_chunk_id = UINT64_MAX;

	/* sort req by coll_idx in map */
	vector<uint32_t> indices(req->core_line_count);
	/* fille with index */
	iota(indices.begin(), indices.end(), 0);
	/* sort by coll_idx */
	sort(indices.begin(), indices.end(), [&](uint32_t x, uint32_t y){
		return req->map[x].coll_idx < req->map[y].coll_idx;
	});

	/*
	 * set the counter to 1 to prevent ocf request from being
	 * freed before the for loop completes
	 */
	env_atomic_set(&req->req_remaining, 1);

	/* calc io handle latency */
	_ocf_engine_update_latency_stats_int_adaptor(req, STATS_CLASS_OCF);
	req->backend_start_timestamp = env_get_tick_count();

	for (uint32_t i = 0; i<indices.size(); i++) {
		if (i == 0) {
			/* calc io handle latency */
			_ocf_engine_update_latency_stats_int_adaptor(req, STATS_CLASS_OCF);
			req->backend_start_timestamp = env_get_tick_count();
		}
		cur = indices[i];
		/* get address */
		addr = req->map[cur].coll_idx;
		addr *= cacheline_size;
		addr += metadata_offset;
		bytes = cacheline_size;

		if (cur == 0) {
			uint64_t seek = req->byte_position % cacheline_size;

			addr += seek;
			/*
			 * when only one cacheline is accessed and the access size is smaller than
			 * the cache line size, bytes need to be directly set to size
			 */
			bytes = ocf_min(bytes - seek, size);
		} else if (cur == (req->core_line_count - 1)) {
			uint64_t skip = (cacheline_size -
				((req->byte_position + size) % cacheline_size))
				% cacheline_size;

			bytes -= skip;
		}

		chunk_id = addr / LAVA_CHUNK_SIZE;
		/* send previous request when chunk ids are not consecutive */
		if (chunk_req != NULL && chunk_id != pre_chunk_id) {
			_do_send_chunk_request(req, chunk_req, dir, blocksize);
			/* reset block size stats */
			blocksize = 0;
			chunk_req = NULL;
		}
		pre_chunk_id = chunk_id;

		/* create a new chunk request when loop begin or previous request was sended */
		if (chunk_req == NULL) {
			chunk_req = new Request();
			chunk_req->chunk_id = chunk_id;
			chunk_req->user_ctx = req;
			chunk_req->cb = lava_volume_submit_req_cb;
		}

		/* check whether the addresses are consecutive */
		if (chunk_req->segments.size() > 0 && i > 0) {
			uint32_t pre_cur = indices[i - 1];
			auto &pre_s = chunk_req->segments.back();
			/* coreline and cacheline are both consecutive */
			if ((pre_s.offset + pre_s.length == addr) &&
					(req->map[cur].core_id == req->map[pre_cur].core_id) &&
					(req->map[cur].core_line > req->map[pre_cur].core_line) &&
					(req->map[cur].core_line == req->map[pre_cur].core_line + 1)) {
				/* merge io */
				pre_s.length += bytes;
				total_bytes += bytes;
				blocksize += bytes;
				continue;
			}
		}

		/* create a new segment when requests are not consecutive */
		Segment s;
		s.offset = addr % LAVA_CHUNK_SIZE;
		s.length = bytes;
		/* locate the start position of the buffer */
		s.data = data->ptr + offset_dict[cur];

		chunk_req->segments.push_back(s);
		total_bytes += bytes;
		blocksize += bytes;
	}

	/* last chunk request */
	if (chunk_req != NULL) {
		_do_send_chunk_request(req, chunk_req, dir, blocksize);
	}

	/* callback for free req_remaining */
	callback(req, 0);

	ENV_BUG_ON(total_bytes != size);
}

/*
 * We don't need to implement submit_flush(). Just complete io with success.
 */
static void lava_volume_submit_flush(struct ocf_io *io)
{
	io->end(io, 0);
}

/*
 * We don't need to implement submit_discard(). Just complete io with success.
 */
static void lava_volume_submit_discard(struct ocf_io *io)
{
	io->end(io, 0);
}

/*
 * Let's set maximum io size to 128 KiB.
 */
static unsigned int lava_volume_get_max_io_size(ocf_volume_t volume)
{
	return 128 * 1024;
}

/*
 * Return volume size.
 */
static uint64_t lava_volume_get_length(ocf_volume_t volume)
{
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);

	return lava_volume->chunk_ids->size() * LAVA_CHUNK_SIZE;
}

/*
 * In set_data() we just assing data and offset to io.
 */
static int lava_volume_io_set_data(struct ocf_io *io, ctx_data_t *data,
		uint32_t offset)
{
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);

	lava_volume_io->data = (struct volume_data*)data;
	lava_volume_io->offset = offset;

	return 0;
}

/*
 * In get_data() return data stored in io.
 */
static ctx_data_t *lava_volume_io_get_data(struct ocf_io *io)
{
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);

	return lava_volume_io->data;
}

static int no_io_volume_open(ocf_volume_t volume, void *volume_params)
{
    struct no_io_volume *v = (struct no_io_volume*)ocf_volume_get_priv(volume);
    v->cache_line_size = *(uint64_t *)volume_params;
    return 0;
}

static void no_io_volume_close(ocf_volume_t volume)
{
}

static void no_io_volume_submit_io(struct ocf_io *io)
{
}

static void no_io_volume_submit_flush(struct ocf_io *io)
{
}

static void no_io_volume_submit_discard(struct ocf_io *io)
{
}

static unsigned int no_io_volume_get_max_io_size(ocf_volume_t volume)
{
    return 128 * 1024;
}

static uint64_t no_io_volume_get_length(ocf_volume_t volume)
{
    struct no_io_volume *v = (struct no_io_volume*)ocf_volume_get_priv(volume);
    return (1ULL << CORE_LINE_BITS) * v->cache_line_size;
}

static int no_io_volume_io_set_data(struct ocf_io *io, ctx_data_t *data,
        uint32_t offset)
{
    return 0;
}


static ctx_data_t *no_io_volume_io_get_data(struct ocf_io *io)
{
    return NULL;
}

/*
 * This structure contains volume properties. It describes volume
 * type, which can be later instantiated as backend storage for cache
 * or core.
 */
static struct ocf_volume_properties volume_properties;

static struct ocf_volume_properties no_io_volume_properties;

/*
 * This function registers volume type in OCF context.
 * It should be called just after context initialization.
 */
int volume_init(ocf_ctx_t ocf_ctx)
{
	volume_properties.name = "Chunk volume",
	volume_properties.io_priv_size = sizeof(struct lava_volume_io),
	volume_properties.volume_priv_size = sizeof(struct lava_volume),
	volume_properties.caps.atomic_writes = 0;

	volume_properties.ops.open = lava_volume_open;
	volume_properties.ops.close = lava_volume_close;
	volume_properties.ops.submit_io = lava_volume_submit_io;
	volume_properties.ops.submit_req = lava_volume_submit_req;
	volume_properties.ops.submit_flush = lava_volume_submit_flush;
	volume_properties.ops.submit_discard = lava_volume_submit_discard;
	volume_properties.ops.get_max_io_size = lava_volume_get_max_io_size;
	volume_properties.ops.get_length = lava_volume_get_length;
	volume_properties.ops.submit_dummy_io = lava_volume_submit_dummy_io;

	volume_properties.io_ops.set_data = lava_volume_io_set_data;
	volume_properties.io_ops.get_data = lava_volume_io_get_data;

	no_io_volume_properties.name = "no io volume",
	no_io_volume_properties.io_priv_size = 0,
	no_io_volume_properties.volume_priv_size = sizeof(struct no_io_volume),
	no_io_volume_properties.caps.atomic_writes = 0;

	no_io_volume_properties.ops.open = no_io_volume_open;
	no_io_volume_properties.ops.close = no_io_volume_close;
	no_io_volume_properties.ops.submit_io = no_io_volume_submit_io;
	no_io_volume_properties.ops.submit_flush = no_io_volume_submit_flush;
	no_io_volume_properties.ops.submit_discard = no_io_volume_submit_discard;
	no_io_volume_properties.ops.get_max_io_size = no_io_volume_get_max_io_size;
	no_io_volume_properties.ops.get_length = no_io_volume_get_length;

	no_io_volume_properties.io_ops.set_data = no_io_volume_io_set_data;
	no_io_volume_properties.io_ops.get_data = no_io_volume_io_get_data;

	int ret = ocf_ctx_register_volume_type(ocf_ctx, LAVA_VOL_TYPE, &volume_properties);
	if (ret) {
		return ret;
	}

	ret = ocf_ctx_register_volume_type(ocf_ctx, CORE_VOL_TYPE, &no_io_volume_properties);

	return ret;
}

/*
 * This function unregisters volume type in OCF context.
 * It should be called just before context cleanup.
 */
void volume_cleanup(ocf_ctx_t ocf_ctx)
{
	ocf_ctx_unregister_volume_type(ocf_ctx, LAVA_VOL_TYPE);
	ocf_ctx_unregister_volume_type(ocf_ctx, CORE_VOL_TYPE);
}