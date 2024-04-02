/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ut for engine_uc file
 * Author: hebo
 * Create: 2024-02-19
 */

#ifndef __UT_CTX_H__
#define __UT_CTX_H__

#include "ocf_env.h"

ctx_data_t *ut_ctx_data_alloc(uint32_t);
void ut_ctx_data_free(ctx_data_t *);
uint32_t ut_ctx_data_zero(ctx_data_t *, uint32_t);

#endif
