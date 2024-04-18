/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ocf/ocf.h"
#include "../ocf_priv.h"
#include "metadata.h"
#include "metadata_core.h"
#include "metadata_internal.h"
#include "metadata_raw.h"

static struct ocf_metadata_map *ocf_metadata_get_collision_map(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	struct ocf_metadata_raw *raw = &(ctrl->raw_desc[metadata_segment_collision]);

	if (ocf_line_size(cache) <= 16 * KiB) {
		struct ocf_metadata_map_u8 *coll_map_u8 = raw->mem_pool;
		struct ocf_metadata_map_u8 *coll = &coll_map_u8[line];
		return &coll->map;
	} else {
		struct ocf_metadata_map_u16 *coll_map_u16 = raw->mem_pool;
		struct ocf_metadata_map_u16 *coll = &coll_map_u16[line];
		return &coll->map;
	}
}

void ocf_metadata_get_core_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t *core_id,
		uint64_t *core_sector)
{
	struct ocf_metadata_map *collision;
	
	collision = ocf_metadata_get_collision_map(cache, line);

	if(core_sector) {
		*core_sector = collision->core_line;
	}

	if(core_id) {
		*core_id = collision->core_id;
	}
}

void ocf_metadata_set_core_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t core_id,
		uint64_t core_sector)
{
	struct ocf_metadata_map *collision;

	collision = ocf_metadata_get_collision_map(cache, line);

	if (collision) {
		collision->core_id = core_id;
		collision->core_line = core_sector;
	} else {
		ocf_metadata_error(cache);
	}
}

ocf_core_id_t ocf_metadata_get_core_id(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	const struct ocf_metadata_map *collision;

	collision = ocf_metadata_get_collision_map(cache, line);

	if (collision)
		return collision->core_id;

	ocf_metadata_error(cache);
	return OCF_CORE_MAX;
}

void ocf_metadata_get_core_and_part_id(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t *core_id,
		ocf_part_id_t *part_id)
{
	const struct ocf_metadata_map *collision = 
			ocf_metadata_get_collision_map(cache, line);
;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	struct ocf_metadata_raw *raw = &(ctrl->raw_desc[metadata_segment_list_info]);

	const struct ocf_metadata_list_info *info = 
			&((struct ocf_metadata_list_info *)raw->mem_pool)[line];

	ENV_BUG_ON(!collision || !info);

	if (core_id)
		*core_id = collision->core_id;
	if (part_id)
		*part_id = info->partition_id;
}

struct ocf_metadata_uuid *ocf_metadata_get_core_uuid(
		struct ocf_cache *cache, ocf_core_id_t core_id)
{
	struct ocf_metadata_uuid *muuid;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	muuid = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_core_uuid]), core_id);

	if (!muuid)
		ocf_metadata_error(cache);

	return muuid;
}
