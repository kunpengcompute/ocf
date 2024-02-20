/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ut for engine_uc file
 * Author: hebo
 * Create: 2024-02-19
 */

#ifndef __VOLUME_H__
#define __VOLUME_H__

#include <ocf/ocf.h>
#include "ocf_env.h"
#include "ctx.h"
#include "data.h"

struct myvolume_io {
	struct volume_data *data;
	uint32_t offset;
};

struct myvolume {
	uint8_t *mem;
	const char *name;
};

int ut_volume_init(ocf_ctx_t ocf_ctx);
void ut_volume_cleanup(ocf_ctx_t ocf_ctx);

#endif
