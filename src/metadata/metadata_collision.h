/*
 * Copyright(c) 2012-2022 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __METADATA_COLLISION_H__
#define __METADATA_COLLISION_H__

/**
 * @brief Metadata map structure
 */

struct ocf_metadata_list_info {
	ocf_cache_line_t next_col : CACHE_LINE_BITS;
		/*!<  Next cache line in collision list*/
	ocf_part_id_t partition_id : 2;
		/*!<  ID of partition where is assigned this cache line*/
} __attribute__((packed));

/**
 * @brief Metadata map structure
 */

struct ocf_metadata_map {
	uint64_t core_line : CORE_LINE_BITS;
		/*!<  Core line addres on cache mapped by this strcture */

	uint16_t core_id : CORE_ID_BITS;
		/*!<  ID of core where is assigned this cache line*/

} __attribute__((packed));

void ocf_metadata_set_collision_info(
		struct ocf_cache *cache, ocf_cache_line_t line,
		ocf_cache_line_t next);

void ocf_metadata_set_collision_next(
		struct ocf_cache *cache, ocf_cache_line_t line,
		ocf_cache_line_t next);

void ocf_metadata_get_collision_info(
		struct ocf_cache *cache, ocf_cache_line_t line,
		ocf_cache_line_t *next);

static inline ocf_cache_line_t ocf_metadata_get_collision_next(
		struct ocf_cache *cache, ocf_cache_line_t line)
{
	ocf_cache_line_t next;

	ocf_metadata_get_collision_info(cache, line, &next);
	return next;
}

void ocf_metadata_add_to_collision(struct ocf_cache *cache,
		ocf_core_id_t core_id, uint64_t core_line,
		ocf_cache_line_t hash, ocf_cache_line_t cache_line);

void ocf_metadata_remove_from_collision(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_part_id_t part_id);

void ocf_metadata_start_collision_shared_access(
		struct ocf_cache *cache, ocf_cache_line_t line);

void ocf_metadata_end_collision_shared_access(
		struct ocf_cache *cache, ocf_cache_line_t line);

#endif /* METADATA_COLLISION_H_ */
