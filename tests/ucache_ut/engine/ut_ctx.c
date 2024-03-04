/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ut for engine_uc file
 * Author: hebo
 * Create: 2024-02-19
 */

#include "ocf_env.h"
#include "volume.h"

#define PAGE_SIZE 4096

ctx_data_t *ut_ctx_data_alloc(uint32_t pages)
{
	struct volume_data *data;

	data = malloc(sizeof(*data));
	data->ptr = malloc(pages * PAGE_SIZE);
	data->offset = 0;

	return data;
}

void ut_ctx_data_free(ctx_data_t *ctx_data)
{
	struct volume_data *data = ctx_data;

	if (!data)
		return;

	free(data->ptr);
	free(data);
}

uint32_t ut_ctx_data_zero(ctx_data_t *dst, uint32_t size)
{
	struct volume_data *data = dst;

	memset(data->ptr + data->offset, 0, size);
	data->offset += size;

	return size;
}
