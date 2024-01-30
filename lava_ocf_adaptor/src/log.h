/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __LOG_H__
#define __LOG_H__

#include "ocf_adaptor_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ocf_adaptor_log(lvl, fmt, ...) \
	log_raw(lvl, fmt, ##__VA_ARGS__)

void set_log_print(log_print_func func);
log_print_func get_log_print();

int log_raw(ocf_log_level lvl, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif