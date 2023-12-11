/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*******************************************************************************
 * Sector mask getter
 ******************************************************************************/

static inline uint64_t _get_mask(uint8_t start, uint8_t stop)
{
	uint64_t mask = 0;

	ENV_BUG_ON(start >= 64);
	ENV_BUG_ON(stop >= 64);
	ENV_BUG_ON(stop < start);

	mask = ~mask;
	mask >>= start + (63 - stop);
	mask <<= start;

	return mask;
}

#define _get_mask_u8(start, stop) _get_mask(start, stop)
#define _get_mask_u16(start, stop) _get_mask(start, stop)

#define ocf_metadata_bit_struct(type) \
struct ocf_metadata_map_##type { \
	struct ocf_metadata_map map; \
	type valid; \
	type dirty; \
} __attribute__((packed))

#define ocf_metadata_bit_func(what, type) \
static bool _ocf_metadata_test_##what##_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all) \
{ \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	const struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	if (all) { \
		if (mask == (map[line].what & mask)) { \
			return true; \
		} else { \
			return false; \
		} \
	} else { \
		if (map[line].what & mask) { \
			return true; \
		} else { \
			return false; \
		} \
	} \
} \
\
static bool _ocf_metadata_test_out_##what##_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	const struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	if (map[line].what & ~mask) { \
		return true; \
	} else { \
		return false; \
	} \
} \
\
static bool _ocf_metadata_clear_##what##_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	map[line].what &= ~mask; \
\
	if (map[line].what) { \
		return true; \
	} else { \
		return false; \
	} \
} \
\
static bool _ocf_metadata_set_##what##_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	bool result; \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	result = map[line].what ? true : false; \
\
	map[line].what |= mask; \
\
	return result; \
} \
\
static bool _ocf_metadata_test_and_set_##what##_##type( \
		struct ocf_cache *cache, ocf_cache_line_t line, \
		uint8_t start, uint8_t stop, bool all) \
{ \
	bool test; \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	if (all) { \
		if (mask == (map[line].what & mask)) { \
			test = true; \
		} else { \
			test = false; \
		} \
	} else { \
		if (map[line].what & mask) { \
			test = true; \
		} else { \
			test = false; \
		} \
	} \
\
	map[line].what |= mask; \
	return test; \
} \
\
static bool _ocf_metadata_test_and_clear_##what##_##type( \
		struct ocf_cache *cache, ocf_cache_line_t line, \
		uint8_t start, uint8_t stop, bool all) \
{ \
	bool test; \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	if (all) { \
		if (mask == (map[line].what & mask)) { \
			test = true; \
		} else { \
			test = false; \
		} \
	} else { \
		if (map[line].what & mask) { \
			test = true; \
		} else { \
			test = false; \
		} \
	} \
\
	map[line].what &= ~mask; \
	return test; \
} \

#define ocf_metadata_bit_func_basic(type) \
static bool _ocf_metadata_clear_valid_if_clean_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	map[line].valid &= (mask & map[line].dirty) | (~mask); \
\
	if (map[line].valid) { \
		return true; \
	} else { \
		return false; \ 
	} \
} \
\
static void _ocf_metadata_clear_dirty_if_invalid_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	type mask = _get_mask_##type(start, stop); \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	map[line].dirty &= (mask & map[line].valid) | (~mask); \
} \
\
/* true if no incorrect combination of status bits */
#define ocf_metadata_bit_check_func(type) \
static bool _ocf_metadata_check_##type(struct ocf_cache *cache, \
		ocf_cache_line_t line) \
{ \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	const struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	/* dirty bits must have valid bit set */ \
	return (map[line].dirty & (~map[line].valid)) == 0; \
} \

#define ocf_metadata_bit_func_basic_no_type() \
static bool _ocf_metadata_clear_valid_if_clean(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map_##type *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
\
	_raw_bug_on(raw, line); \
\
	map[line]._valid = (!map[line]._dirty)? 0 :  map[line]._valid; \
\
	if (map[line]._valid) { \
		return true; \
	} else { \
		return false; \ 
	} \
} \
\
static void _ocf_metadata_clear_dirty_if_invalid(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
\
	_raw_bug_on(raw, line); \
\
	map[line]._dirty = (!map[line]._valid)? 0 : map[line]._dirty;\
} \
\
/* true if no incorrect combination of status bits */ \
static bool _ocf_metadata_check(struct ocf_cache *cache, \
		ocf_cache_line_t line) \
{ \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map *map = raw->mem_pool; \
\
	_raw_bug_on(raw, line); \
\
	return (map[line]._dirty & (!map[line]._valid)) == 0; \
} \

#define ocf_metadata_bit_func_no_type(what) \
static bool _ocf_metadata_test_##what(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all) \
{ \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	const struct ocf_metadata_map *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
\
	_raw_bug_on(raw, line); \
\
	if (map[line]._##what) { \
		return true; \
	} else { \
		return false; \ 
	} \
} \
\
static bool _ocf_metadata_test_out_##what(struct ocf_cache *cache, \
	ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	return false; \
} \
\
static bool _ocf_metadata_clear_##what(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
\
	_raw_bug_on(raw, line); \
\
	map[line]._##what = 0; \
\
	return false; \
} \
\
static bool _ocf_metadata_set_##what(struct ocf_cache *cache, \
		ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	bool result; \
\
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
	_raw_bug_on(raw, line); \
\
	result = map[line]._##what ? true : false; \
\
	map[line]._##what = 1; \
\
	return result; \
} \
\
static bool _ocf_metadata_test_and_set_##what( \
		struct ocf_cache *cache, ocf_cache_line_t line, \
		uint8_t start, uint8_t stop, bool all) \
{ \
	bool test; \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
\
	_raw_bug_on(raw, line); \
\
	if (map[line]._##what) { \
		test = true; \
	} else { \
		test = false; \
	} \
\
	map[line]._##what = 1; \
	return test; \
} \
\
static bool _ocf_metadata_test_and_clear_##what( \
		struct ocf_cache *cache, ocf_cache_line_t line, \
		uint8_t start, uint8_t stop, bool all) \
{ \
	bool test; \
	struct ocf_metadata_ctrl *ctrl = \
		(struct ocf_metadata_ctrl *) cache->metadata.priv; \
\
	struct ocf_metadata_raw *raw = \
			&ctrl->raw_desc[metadata_segment_collision]; \
\
	struct ocf_metadata_map *map = raw->mem_pool; \
\
	ENV_BUG_ON(start != stop); \
\
	_raw_bug_on(raw, line); \
\
	if (map[line]._##what) { \
		test = true; \
	} else { \
		test = false; \
	} \
\
	map[line]._##what = 0; \
	return test; \
} \

#define ocf_metadata_bit_funcs(type) \
ocf_metadata_bit_struct(type); \
ocf_metadata_bit_func(dirty, type); \
ocf_metadata_bit_func(valid, type); \
ocf_metadata_bit_func_basic(type); \

#define ocf_metadata_bit_no_struct() \
ocf_metadata_bit_func_no_type(dirty); \
ocf_metadata_bit_func_no_type(valid); \
ocf_metadata_bit_func_basic_no_type(); \

ocf_metadata_bit_no_struct();

ocf_metadata_bit_funcs(u8);
ocf_metadata_bit_funcs(u16);

ocf_metadata_bit_check_func(u8);
ocf_metadata_bit_check_func(u16);