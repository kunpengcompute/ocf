/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SLOT_INFO_H__
#define __SLOT_INFO_H__

#include <ocf/ocf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REGION_NUM_LIMIT (1 << 16) // 16k
#define REGION_REMAP_MASK ((1 << 16) - 1)

struct slot_info {
	ocf_core_t core;
	int now;
	uint8_t isUsed[REGION_NUM_LIMIT + 1];
};

typedef struct slot_info *slot_info_t;

/* does'nt support concurrency, concurrent locking is implemented by the upper layer */
int get_remap_id(slot_info_t info); // return value range [1, REGION_NUM_LIMIT]
void put_remap_id(slot_info_t info, int remap_id);

#ifdef __cplusplus
}
#endif
#endif