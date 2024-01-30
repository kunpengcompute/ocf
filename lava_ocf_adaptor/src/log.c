/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <syslog.h>
#include <stdio.h>
#include "log.h"

#define MAX_BUF 1024

static int default_log_print(ocf_log_level lvl, const char *message)
{
	int severity = lvl;	
	syslog(severity, "%s", message);
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
	char buf[MAX_BUF];
	va_list args;
	va_start(args, fmt);
	int ret = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (ret < 0) {
		g_log_print(OCF_LOG_ERROR, "the print is too long\n");
		return -1;
	}
	return g_log_print(lvl, buf);
}