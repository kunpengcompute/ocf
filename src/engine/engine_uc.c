/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: new cache mode engine: UCache interface
 * Create: 2024-01-27
 */

#include "engine_common.h"
#include "engine_inv.h"
#include "engine_uc.h"

#include "../ocf_request.h"
#include "../utils/utils_io.h"
#include "../utils/utils_user_part.h"

#define OCF_ENGINE_DEBUG 0

#define OCF_ENGINE_DEBUG_IO_NAME "uc"
#include "engine_debug.h"

static void _ocf_read_ucache_hit_complete(struct ocf_request *req, int error)
{
	struct ocf_alock *c = ocf_cache_line_concurrency(
		req->cache);

	if (error)
		req->error |= error;

	/* Handle callback-caller race to let only one of the two complete the
	 * request. Also, complete original request only if this is the last
	 * sub-request to complete
	 */
	if (env_atomic_dec_return(&req->req_remaining) == 0) {
		OCF_DEBUG_RQ(req, "HIT completion");

		if (req->error) {
			ocf_core_stats_cache_error_update(req->core, OCF_READ);
			OCF_DEBUG_RQ(req, "read cache error: %d", req->error);
			req->error = -OCF_ERR_UCACHE_IO;
		}

		ocf_req_unlock(c, req);

		/* Complete request */
		req->complete(req, req->error);

		/* Free the request at the last point
		 * of the completion path
		 */
		ocf_req_put(req);
	}
}

static void ocf_read_ucache_submit_hit(struct ocf_request *req)
{
	env_atomic_set(&req->req_remaining, ocf_engine_io_count(req));

	ocf_submit_cache_reqs(req->cache, req, OCF_READ, 0, req->byte_length,
		ocf_engine_io_count(req), _ocf_read_ucache_hit_complete);
}

static int _ocf_read_ucache_do(struct ocf_request *req)
{
	if (ocf_engine_is_miss(req)) {
		/* Cache Miss */
		OCF_DEBUG_RQ(req, "Cache Miss, Read Canceled");
		req->complete(req, -OCF_ERR_CACHE_MISS);
		ocf_req_unlock(ocf_cache_line_concurrency(req->cache), req);
		ocf_req_put(req);
		return 0;
	}

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	if (ocf_engine_needs_repart(req)) {
		OCF_DEBUG_RQ(req, "Re-Part");

		ocf_hb_req_prot_lock_wr(req);

		/* Probably some cache lines are assigned into wrong
		 * partition. Need to move it to new one
		 */
		ocf_user_part_move(req);

		ocf_hb_req_prot_unlock_wr(req);
	}

	OCF_DEBUG_RQ(req, "Submit");

	/* Submit IO */
	ocf_read_ucache_submit_hit(req);

	/* Update statistics */
	ocf_engine_update_request_stats(req);
	ocf_engine_update_block_stats(req);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static void _ocf_lookup_ucache(struct ocf_request *req)
{
	ocf_req_hash(req);
	ocf_hb_req_prot_lock_rd(req);
	ocf_engine_lookup(req);
	ocf_hb_req_prot_unlock_rd(req);
	if (ocf_engine_is_hit(req)) {
		req->complete(req, 0);
	} else {
		req->complete(req, -OCF_ERR_CACHE_MISS);
	}

	ocf_req_put(req);
}

static const struct ocf_io_if _io_if_read_ucache_resume = {
	.read = _ocf_read_ucache_do,
	.write = _ocf_read_ucache_do,
};

static const struct ocf_engine_callbacks _uc_read_engine_callbacks =
{
	.resume = ocf_engine_on_resume,
};

int ocf_read_ucache(struct ocf_request *req)
{
	int lock = OCF_LOCK_NOT_ACQUIRED;

	/* LOOKUP */
	if (req->ioi.io.flags == OCF_LOOKUP) {
		_ocf_lookup_ucache(req);
		return 0;
	}

	ocf_io_start(&req->ioi.io);

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	/* Set resume call backs */
	req->io_if = &_io_if_read_ucache_resume;
	req->engine_cbs = &_uc_read_engine_callbacks;

	lock = ocf_engine_prepare_clines(req);

	if (!ocf_req_test_mapping_error(req)) {
		if (lock >= 0) {
			if (lock != OCF_LOCK_ACQUIRED) {
				/* Lock was not acquired, need to wait for resume */
				OCF_DEBUG_RQ(req, "NO LOCK");
			} else {
				/* Lock was acquired can perform IO */
				_ocf_read_ucache_do(req);
			}
		} else {
			OCF_DEBUG_RQ(req, "LOCK ERROR %d", lock);
			req->complete(req, lock);
			ocf_req_put(req);
		}
	} else {
		ocf_req_clear(req);
		OCF_DEBUG_RQ(req, "MAP ERROR");
		req->complete(req, -OCF_ERR_NO_LOCK);
		ocf_req_put(req);
	}

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static void _ocf_write_uc_cache_complete(struct ocf_request *req, int error)
{
	if (error) {
		req->error = req->error ?: error;
		ocf_core_stats_cache_error_update(req->core, OCF_WRITE);
	}

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_RQ(req, "Completion");

	if (req->error) {
		/* An error occured */
		OCF_DEBUG_RQ(req, "write cache error: %d", req->error);

		/* Complete request */
		req->complete(req, -OCF_ERR_UCACHE_IO);

		ocf_engine_invalidate(req);

		return;
	}

	ocf_req_unlock_wr(ocf_cache_line_concurrency(req->cache), req);

	req->complete(req, 0);

	ocf_req_put(req);
}

static inline void _ocf_write_uc_submit(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;

	/* Submit IOs */
	OCF_DEBUG_RQ(req, "Submit");

	/* Calculate how many IOs need to be submited */
	env_atomic_set(&req->req_remaining, ocf_engine_io_count(req)); /* Cache IO */

	/* To cache */
	ocf_submit_cache_reqs(cache, req, OCF_WRITE, 0, req->byte_length,
			ocf_engine_io_count(req), _ocf_write_uc_cache_complete);
}

static void _ocf_write_uc_update_bits(struct ocf_request *req)
{
	bool miss = ocf_engine_is_miss(req);
	bool repart = ocf_engine_needs_repart(req);

	if (!miss && !repart)
		return;

	ocf_hb_req_prot_lock_wr(req);

	if (miss) {
		/* Update valid status bits */
		ocf_set_valid_map_info(req);
	}

	if (repart) {
		OCF_DEBUG_RQ(req, "Re-Part");
		/* Probably some cache lines are assigned into wrong
		 * partition. Need to move it to new one
		 */
		ocf_user_part_move(req);
	}

	ocf_hb_req_prot_unlock_wr(req);
}

static int _ocf_invalid_write_do(struct ocf_request *req)
{
	req->complete(req, 0);
	ocf_engine_invalidate(req);
	return 0;
}

static const struct ocf_io_if _io_if_invalid_uc_resume = {
	.read = _ocf_invalid_write_do,
	.write = _ocf_invalid_write_do,
};

static const struct ocf_engine_callbacks _uc_invalid_engine_callbacks =
{
	.resume = ocf_engine_on_resume,
};

static int _ocf_invalid_write_ucache(struct ocf_request *req)
{
	int lock = OCF_LOCK_NOT_ACQUIRED;

	ocf_io_start(&req->ioi.io);

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	/* Set resume io_if */
	req->io_if = &_io_if_invalid_uc_resume;
	req->engine_cbs = &_uc_invalid_engine_callbacks;

	/* only get lock already mapped */
	lock = ocf_engine_get_mapped_lock(req);

	if (lock >= 0) {
		if (lock != OCF_LOCK_ACQUIRED) {
			/* WR lock was not acquired, need to wait for resume */
			OCF_DEBUG_RQ(req, "NO LOCK");
		} else {
			_ocf_invalid_write_do(req);
		}
	} else {
		OCF_DEBUG_RQ(req, "LOCK ERROR %d\n", lock);
		req->complete(req, lock);
		ocf_req_put(req);
	}

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static int _ocf_write_uc_do(struct ocf_request *req)
{
	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	_ocf_write_uc_update_bits(req);

	/* Submit IO */
	_ocf_write_uc_submit(req);

	/* Update statistics */
	ocf_engine_update_request_stats(req);
	ocf_engine_update_block_stats(req);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static const struct ocf_io_if _io_if_uc_resume = {
	.read = _ocf_write_uc_do,
	.write = _ocf_write_uc_do,
};

static const struct ocf_engine_callbacks _uc_write_engine_callbacks =
{
	.resume = ocf_engine_on_resume,
};

int ocf_write_ucache(struct ocf_request *req)
{
	/* INVALID write */
	if (req->ioi.io.flags == OCF_INVALID) {
		_ocf_invalid_write_ucache(req);
		return 0;
	}

	int lock = OCF_LOCK_NOT_ACQUIRED;

	ocf_io_start(&req->ioi.io);

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	/* Set resume io_if */
	req->io_if = &_io_if_uc_resume;
	req->engine_cbs = &_uc_write_engine_callbacks;

	lock = ocf_engine_prepare_clines(req);

	if (!ocf_req_test_mapping_error(req)) {
		if (lock >= 0) {
			if (lock != OCF_LOCK_ACQUIRED) {
				/* WR lock was not acquired, need to wait for resume */
				OCF_DEBUG_RQ(req, "NO LOCK");
			} else {
				_ocf_write_uc_do(req);
			}
		} else {
			OCF_DEBUG_RQ(req, "LOCK ERROR %d\n", lock);
			req->complete(req, lock);
			ocf_req_put(req);
		}
	} else {
		OCF_DEBUG_RQ(req, "MAP ERROR %d\n", lock);
		ocf_req_clear(req);
		req->complete(req, -OCF_ERR_NO_LOCK);
		ocf_req_put(req);
	}

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}