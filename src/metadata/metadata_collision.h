/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_COLLISION_H__
#define __METADATA_COLLISION_H__

/**
 * @brief Metadata map structure
 */

struct ocf_metadata_list_info {
		/*!<  Previous cache line in collision list */
	ocf_cache_line_t next_col : CACHE_LINE_BITS;
		/*!<  Next cache line in collision list*/
	ocf_part_id_t partition_id : 6;
		/*!<  ID of partition where is assigned this cache line*/
} __attribute__((packed));

/**
 * @brief Metadata map structure
 */

struct ocf_metadata_map {
	/* Core line is aligned to PAGE_SIZE in the worst case, so we don't keep
	 * the least significant bits (12) that are all zeros.
	 * Largest supported volume is 64 TB. */
	uint64_t core_line : CORE_LINE_BITS;

	uint64_t core_id : CORE_ID_BITS;
		/*!<  ID of core where is assigned this cache line*/
	uint16_t _valid : 1;
		/*!<  valid bit for 4K cache line */
	uint16_t _dirty : 1;
		/*!<  dirty bit for 4K cache line */
} __attribute__((packed));

ocf_cache_line_t ocf_metadata_map_lg2phy(
		struct ocf_cache *cache, ocf_cache_line_t coll_idx);

ocf_cache_line_t ocf_metadata_map_phy2lg(
		struct ocf_cache *cache, ocf_cache_line_t cache_line);

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
