/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "log.h"
#include "slot_info.h"

int get_remap_id(slot_info_t info)
{
	int now = info->now;
	uint8_t *isUsed = info->isUsed;
	for (int i = 0; i < REGION_NUM_LIMIT; ++i) {
		int remap_id = now + 1;
		if (!isUsed[remap_id]) {
			isUsed[remap_id] = 1;
			info->now = (remap_id & REGION_REMAP_MASK);
			return remap_id;
		}
		now = (remap_id & REGION_REMAP_MASK);
	}
	return -1;
}

void put_remap_id(slot_info_t info, int remap_id)
{
	if (remap_id <= 0 || remap_id > REGION_NUM_LIMIT) {
		ocf_adaptor_log(OCF_LOG_ERROR, "remap_id(%d) is invalid\n", remap_id);
		return;
	}
	info->isUsed[remap_id] = 0;
}