/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCF_STATUS_NONE         0
#define OCF_STATUS_INITIALIZED  1
#define OCF_STATUS_DELETING     2
#define OCF_STATUS_ERROR        3

/**
 * @brief get ocf status
 *
 * @retval g_status.state
 */
int get_ocf_global_status();

/**
 * @brief set ocf status
 *
 * @param[in] status: set g_status.state
 */
void set_ocf_global_status(int status);

/**
 * @brief set ocf timeout val
 *
 * @param[in] val: set g_status.ocf_timeout value, unit is microsecond
 */
void set_ocf_check_timeout_val(uint64_t val);


/**
 * @brief get valid core from core_mask
 *
 * @retval valid core count
 */
int select_valid_cpu_core(__uint128_t core_mask, uint8_t *cpu_valid_core);

#ifdef __cplusplus
}
#endif
