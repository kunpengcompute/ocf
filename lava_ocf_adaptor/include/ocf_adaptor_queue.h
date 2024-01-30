/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OCF_ADAPTOR_QUEUE_H__
#define __OCF_ADAPTOR_QUEUE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct req_context {
	void *req_identifier;
	/*!< pointer to request context */

	uint32_t io_worker_id;

	uint32_t slot_id;

	uint64_t region_id;

	uint64_t offset;

	uint64_t len;

	char *buffer;

	int (*cb)(int32_t ret, void *ctx);
	/*!< request completion callback; ctx pointer to req_context */
};

#ifdef __cplusplus
}
#endif
#endif
