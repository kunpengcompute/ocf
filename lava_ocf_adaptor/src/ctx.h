/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __CTX_H__
#define __CTX_H__

#include <ocf/ocf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOL_TYPE 1

int ctx_init(ocf_ctx_t *ocf_ctx);
void ctx_cleanup(ocf_ctx_t ctx);

#ifdef __cplusplus
}
#endif
#endif