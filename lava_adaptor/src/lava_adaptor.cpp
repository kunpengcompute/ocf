/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "lava_adaptor.h"

int32_t ocf_init(struct ocf_config *cfg)
{
	return STATE_SUCCESS;
}

int32_t ocf_add_core(uint32_t slot_id)
{
	return STATE_SUCCESS;
}

int32_t ocf_remove_core(uint32_t slot_id)
{
	return STATE_SUCCESS;
}

int32_t ocf_region_invalid(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_range_invalid(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_lookup(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_get(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_put(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_poll(uint32_t io_worker_id, uint32_t max_num)
{
	return STATE_SUCCESS;
}