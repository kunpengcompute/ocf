/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __EVICTION_LRU_STRUCTS_H__

#define __EVICTION_LRU_STRUCTS_H__

/* support max 256TB for 8 KiB Cacheline*/
#define CACHE_MAX_SUPPORT_IN_TB 256

/* support max 4096TB for 8 KiB Cacheline*/
#define CORE_MAX_SUPPORT_IN_TB 4096

#define CACHE_LINE_BITS (27 + __builtin_ctz(CACHE_MAX_SUPPORT_IN_TB))
#define CORE_LINE_BITS (27 + __builtin_ctz(CORE_MAX_SUPPORT_IN_TB))

struct ocf_lru_meta {
	ocf_cache_line_t prev : CACHE_LINE_BITS;
	ocf_cache_line_t next : CACHE_LINE_BITS;
	uint8_t hot : 1;
} __attribute__((packed));

struct ocf_lru_list {
	ocf_cache_line_t num_nodes;
	ocf_cache_line_t head;
	ocf_cache_line_t tail;
	ocf_cache_line_t num_hot;
	ocf_cache_line_t last_hot;
	bool track_hot;
};

struct ocf_lru_part_meta {
	struct ocf_lru_list clean;
	struct ocf_lru_list dirty;
};

#define OCF_LRU_HOT_RATIO 2

#endif
