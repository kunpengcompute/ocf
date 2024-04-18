/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "metadata_eviction_policy.h"
#include "metadata_internal.h"

/*
 * Eviction policy - Get
 */
struct ocf_lru_meta * ocf_metadata_get_lru(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl
		= (struct ocf_metadata_ctrl *) cache->metadata.priv;

	struct ocf_metadata_raw *raw = &(ctrl->raw_desc[metadata_segment_lru]);

	struct ocf_lru_meta *lru_metas = (struct ocf_lru_meta *)raw->mem_pool;

	return &lru_metas[line];
}


