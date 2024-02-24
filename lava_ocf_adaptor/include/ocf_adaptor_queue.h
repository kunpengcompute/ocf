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

	uint32_t region_id;

	uint64_t offset;
	/*!< region offset, need 4k align*/

	uint64_t len;
	/*!< data len, need 4k align*/

	char *buffer;

	int (*cb)(int32_t ret, struct req_context *ctx);
	/*!< request completion callback*/

	char internal[40];
	/*!< internal use of ocf, no need to set */
};

#ifdef __cplusplus
}
#endif
#endif
