/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __VOLUME_H__
#define __VOLUME_H__

#include <ocf/ocf.h>
#include "ocf_env.h"
#include "ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

int volume_init(ocf_ctx_t ocf_ctx);
void volume_cleanup(ocf_ctx_t ocf_ctx);

#ifdef __cplusplus
}
#endif
#endif
