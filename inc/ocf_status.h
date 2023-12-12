/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: set ocf status
 * Create: 2023-11-28
 */

#ifndef SPDK_NVMF_EXTERNAL_H
#define SPDK_NVMF_EXTERNAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


bool spdk_nvmf_get_ocf_status(void);
void spdk_nvmf_set_ocf_status(bool status);

#ifdef __cplusplus
}
#endif

#endif