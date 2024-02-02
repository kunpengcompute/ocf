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
	int remap_id = -1;
	for (int i = 0; i < REGION_NUM_LIMIT; ++i) {
		if (!isUsed[now]) {
			remap_id = now;
			now = ((now + 1) & REGION_REMAP_MASK);
			break;
		}
		now = ((now + 1) & REGION_REMAP_MASK);
	}
	return remap_id;
}