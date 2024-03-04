/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <syslog.h>
#include <stdio.h>
#include "ocf_env.h"
#include "log.h"

#define MAX_BUF 1024

static int default_log_print(ocf_log_level lvl, const char *fmt, va_list args)
{
	if (lvl > OCF_LOG_INFO) {
		return 0;
	}

	int severity = lvl;
	char buf[MAX_BUF];
	int ret = vsnprintf(buf, MAX_BUF, fmt, args);
	if (unlikely(ret < 0)) {
		syslog(OCF_LOG_ERROR, "print log is too long\n");
	} else {
		syslog(severity, "%s", buf);
	}
	return 0;
}

static log_print_func g_log_print = default_log_print;

void set_log_print(log_print_func func)
{
	g_log_print = func;
}

log_print_func get_log_print()
{
	return g_log_print;
}

int log_raw(ocf_log_level lvl, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int ret = g_log_print(lvl, fmt, args);
	va_end(args);
	return ret;
}