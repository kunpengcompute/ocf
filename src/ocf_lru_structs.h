/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __EVICTION_LRU_STRUCTS_H__

#define __EVICTION_LRU_STRUCTS_H__

/* For 4 kB cache lines: 29 bits are enough for up to 2TB cache devices */
#define CACHE_LINE_BITS	29

/* For 4 kB core lines: 34 bits are enough for up to 64TB core devices */
#define CORE_LINE_BITS	34

/* Support 4095 core volumes */
#define CORE_ID_BITS	12
#if OCF_CONFIG_MAX_CORES >= (1 << CORE_ID_BITS)
#error "OCF_CONFIG_MAX_CORES must be less than 1 << CORE_ID_BITS"
#endif

struct ocf_lru_meta {
	uint32_t prev   :CACHE_LINE_BITS,
		 unused1:3;
	uint32_t next   :CACHE_LINE_BITS,
		 hot    :1,
		 unused2:2;
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
