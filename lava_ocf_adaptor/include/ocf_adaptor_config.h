/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OCF_ADAPTOR_CONFIG_H__
#define __OCF_ADAPTOR_CONFIG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __uint128_t uint128_t;

struct ocf_config {
	uint16_t io_worker_num;
	/*!< Number of threads that will submit I/O request */

	uint16_t core_num;
	/*!< Number of cores allocated to ocf */

	uint64_t cache_line_size;

	uint64_t cache_capacity;
	/*!< Cache space size, dynamic modification after initialization is not supported */

	uint64_t chunk_pool_id;

	uint128_t core_mask;
	/*!< set of cores allocated to ocf */

	log_print_func log_print;
	/*!< if log_print is NULL, the log is printed to /var/log/message */
};

#ifdef __cplusplus
}
#endif
#endif