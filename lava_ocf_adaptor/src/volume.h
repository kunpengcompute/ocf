/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __VOLUME_H__
#define __VOLUME_H__

#include <ocf/ocf.h>
#include "ocf_env.h"
#include "ctx.h"

#define LAVA_CHUNK_SIZE (128 * MiB)

#ifdef __cplusplus
extern "C" {
#endif

struct volume_data {
    void *ptr;
    int offset;
};

struct lava_volume_io {
    struct volume_data *data;
    uint32_t offset;
    uint16_t req_cnt;
};

struct lava_volume_param {
    uint32_t chunk_num;
};

int volume_init(ocf_ctx_t ocf_ctx);
void volume_cleanup(ocf_ctx_t ocf_ctx);

#ifdef __cplusplus
}
#endif
#endif
