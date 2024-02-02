/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OCF_ADAPTOR_ERR_H__
#define __OCF_ADAPTOR_ERR_H__

#define STATE_SUCCESS 0

#define STATE_FAIL 1

#define STATE_HIT 2

#define STATE_MISS 3

#define STATE_CORE_EXIST 4

#define STATE_CORE_NOT_EXIST 5

#define STATE_CORE_CREATING  6  // core is creaing, can not remove

#define STATE_TIMEOUT 7

#define STATE_PRRAM_INVALID 8

#define STATE_MEM_ALLOC_ERR 9

#define STATE_TOO_MANY_REGION 10

#define STATE_CACHE_UNAVAILABLE 10000

#endif