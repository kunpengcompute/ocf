/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OCF_ADAPTOR_LOG_H__
#define __OCF_ADAPTOR_LOG_H__

#include <stdarg.h>

typedef enum {
	OCF_LOG_EMERG,
	OCF_LOG_ALERT,
	OCF_LOG_CRIT,
	OCF_LOG_ERROR,
	OCF_LOG_WARN,
	OCF_LOG_NOTICE,
	OCF_LOG_INFO,
	OCF_LOG_DEBUG,
} ocf_log_level;

typedef int (*log_print_func)(ocf_log_level lvl, const char *message);

#endif