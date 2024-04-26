/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OCF_ADAPTOR_ERR_H__
#define __OCF_ADAPTOR_ERR_H__

#define STATE_SUCCESS 0

/***error code for ocf processing result after request submission***/
#define STATE_FAIL -1

#define STATE_MISS -2

#define STATE_CHUNK_TIMEOUT -3

#define STATE_CHUNK_UNAVAILABLE -4

/***error code when submitting request***/
#define STATE_CORE_EXIST -1000

#define STATE_CORE_NOT_EXIST -1001

#define STATE_CORE_CREATING  -1002  // core is creaing, can not remove

#define STATE_PARAM_INVALID -1003

#define STATE_MEM_ALLOC_ERR -1004

#define STATE_TOO_MANY_REGION -1005

#define STATE_REGION_NOT_EXIST -1006

#define STATE_OCF_UNAVAILABLE -1007

#endif