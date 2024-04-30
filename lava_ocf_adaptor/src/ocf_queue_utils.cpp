/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ocf_queue_utils.h"

struct ocf_status_context {
	int state = OCF_STATUS_NONE;
	uint64_t ocf_timeout = 5000000;
} g_status;

int select_valid_cpu_core(__uint128_t core_mask, uint8_t *cpu_valid_core)
{
	int i = 0;
	uint8_t idx = 0;

	for (; idx < 128; ++idx) {
		if (((__uint128_t)1 << (idx)) & core_mask) {
			cpu_valid_core[i++] = idx;
		}
	}

	return i;
}

int get_ocf_global_status()
{
	return g_status.state;
}

void set_ocf_global_status(int status)
{
	g_status.state = status;
}

uint64_t get_ocf_check_timeout_val()
{
	return g_status.ocf_timeout;
}

void set_ocf_check_timeout_val(uint64_t val)
{
	g_status.ocf_timeout = val;
}