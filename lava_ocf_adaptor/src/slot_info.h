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
	uint8_t isUsed[REGION_NUM_LIMIT];
};

typedef struct slot_info *slot_info_t;

int get_remap_id(slot_info_t info);

#ifdef __cplusplus
}
#endif
#endif