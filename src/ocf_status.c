/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: set ocf status
 * Create: 2023-11-28
 */
#include <stdbool.h>

static bool g_ocf_status = true;

bool spdk_nvmf_get_ocf_status(void)
{
    return __atomic_load_n(&g_ocf_status, __ATOMIC_RELAXED);
}

bool spdk_nvmf_set_ocf_status(bool status)
{
    return __atomic_store_n(&g_ocf_status, status, __ATOMIC_RELAXED);
}