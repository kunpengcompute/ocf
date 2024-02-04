/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "slot_info.h"

int get_remap_id(slot_info_t info)
{
	int now = info->now;
	uint8_t *isUsed = info->isUsed;
	for (int i = 0; i < REGION_NUM_LIMIT; ++i) {
		if (!isUsed[now]) {
			isUsed[now] = 1;
			info->now = ((now + 1) & REGION_REMAP_MASK);
			return now;
		}
		now = ((now + 1) & REGION_REMAP_MASK);
	}
	return -1;
}