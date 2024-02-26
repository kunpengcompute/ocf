/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __EVICTION_LRU_STRUCTS_H__

#define __EVICTION_LRU_STRUCTS_H__

#define CACHE_SUPPORT_IN_TB 4

/* 16384 means 2^14 */
#define CORE_SUPPORT_IN_TB 16384

#define CACHE_LINE_BITS (28 + __builtin_ctz(CACHE_SUPPORT_IN_TB))
#define CORE_LINE_BITS (28 + __builtin_ctz(CORE_SUPPORT_IN_TB))

#define CORE_ID_BITS 12

#if OCF_CONFIG_MAX_CORES >= (1 << CORE_ID_BITS)
#error "OCF_CONFIG_MAX_CORES must be less than 1 << CORE_ID_BITS"
#endif

#if CACHE_LINE_BITS >= 32
#error "CACHE_LINE_BITS must be less than 1 << HASH_LOCK_BIT"
#endif

struct ocf_lru_meta {
	uint32_t prev : CACHE_LINE_BITS;
	uint32_t next : CACHE_LINE_BITS;
	uint8_t hot : 1;
} __attribute__((packed));

struct ocf_lru_list {
	uint32_t num_nodes;
	uint32_t head;
	uint32_t tail;
	uint32_t num_hot;
	uint32_t last_hot;
	bool track_hot;
};

struct ocf_lru_part_meta {
	struct ocf_lru_list clean;
	struct ocf_lru_list dirty;
};

#define OCF_LRU_HOT_RATIO 2

#endif
