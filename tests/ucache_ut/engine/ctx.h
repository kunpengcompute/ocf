/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ut for engine_uc file
 * Author: hebo
 * Create: 2024-02-19
 */

#ifndef __CTX_H__
#define __CTX_H__

#include <ocf/ocf.h>

#define VOL_TYPE 1

ctx_data_t *ut_ctx_data_alloc(uint32_t pages);
void ut_ctx_data_free(ctx_data_t *ctx_data);

int ut_ctx_init(ocf_ctx_t *ocf_ctx);
void ut_ctx_cleanup(ocf_ctx_t ctx);

#endif
