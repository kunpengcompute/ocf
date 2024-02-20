/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OCF_ADAPTOR_H__
#define __OCF_ADAPTOR_H__

#include "ocf_adaptor_cli.h"
#include "ocf_adaptor_config.h"
#include "ocf_adaptor_err.h"
#include "ocf_adaptor_log.h"
#include "ocf_adaptor_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ocf module initialization
 *
 * @param[in] cfg: ocf configuration parameters
 *
 * @retval STATE_SUCCESS when success, othewise STATE_FAIL
 */
int ocf_init(struct ocf_config *cfg);

/**
 * @brief ocf module exit process
 */
void ocf_exit();

/**
 * @brief adding a core device corresponding to a slot
 *
 * @param[in] slot_id: unique id of a slot
 *
 * @retval STATE_SUCCESS when the core device is added successfully
 *         STATE_CORE_EXIST when the core device corresponding to the slot already exists
 *         othewise STATE_FAIL
 */
int ocf_add_core(uint32_t slot_id);

/**
 * @brief removing a core device corresponding to a slot
 *
 * @param[in] slot_id: unique id of a slot
 *
 * @retval STATE_SUCCESS when the core device is removed successfully
 *         STATE_CORE_NOT_EXIST when the core device corresponding to the slot doesn't exists
 *         othewise STATE_FAIL
 */
int ocf_remove_core(uint32_t slot_id);

/**
 * @brief clear the entire region data in the cache
 *
 * @param[in] ctx: user request context, this interface does not require setting offset and len
 *
 * @retval STATE_SUCCESS when the invalid request is successfully submmitted
 *         STATE_CORE_NOT_EXIST when the core device corresponding to the slot doesn't exists
 *         othewise STATE_FAIL
 */
int ocf_region_invalid(struct req_context *ctx);

/**
 * @brief clear the specified region segment data in the cache
 *
 * @param[in] ctx: user request context
 *
 * @retval STATE_SUCCESS when the invalid request is successfully submmitted
 *         STATE_CORE_NOT_EXIST when the core device corresponding to the slot doesn't exists
 *         othewise STATE_FAIL
 */
int ocf_range_invalid(struct req_context *ctx);

/**
 * @brief check whether the specified segment data is hit in the cache
 *
 * @param[in] ctx: user request context
 *
 * @retval STATE_SUCCESS when the lookup request is successfully submmitted
 *         STATE_CORE_NOT_EXIST when the core device corresponding to the slot doesn't exists
 *         othewise STATE_FAIL
 */
int ocf_lookup(struct req_context *ctx);

/**
 * @brief read the specified segment data from the cache
 *
 * @param[in] ctx: user request context
 *
 * @retval STATE_SUCCESS when the read request is successfully submmitted
 *         STATE_CORE_NOT_EXIST when the core device corresponding to the slot doesn't exists
 *         othewise STATE_FAIL
 */
int ocf_get(struct req_context *ctx);

/**
 * @brief write the specified segment data to the cache
 *
 * @param[in] ctx: user request context
 *
 * @retval STATE_SUCCESS when the write request is successfully submmitted
 *         STATE_CORE_NOT_EXIST when the core device corresponding to the slot doesn't exists
 *         othewise STATE_FAIL
 */
int ocf_put(struct req_context *ctx);

/**
 * @brief triggers processing of completion queue entries
 *
 * @param[in] io_worker_id: unique id of the io worker, which is used to index the io worker submission queue
 * @param[in] max_num: maximum number of cq entries per time
 *
 * @retval STATE_FAIL when the cq of the io worker doesn't exist
 *         othewise STATE_SUCCESS
 */
int ocf_poll(uint32_t io_worker_id, int max_num);

/**
 * @brief dump basic cache information and the mapping between caches and cores device
 *
 * @retval NULL when ocf_dump_info malloc fail, otherwise ocf_dump_info pointer
 */
struct ocf_dump_info *ocf_dump_cache_core_info();

/**
 * @brief dump cache statistic information
 *
 * @retval NULL when ocf_dump_info memory malloc fail, otherwise ocf_dump_info pointer
 */
struct ocf_dump_info *ocf_dump_cache_stats();

/**
 * @brief release the memory for storing dump information
 */
void ocf_release_dump_info(struct ocf_dump_info *info);

#ifdef __cplusplus
}
#endif
#endif