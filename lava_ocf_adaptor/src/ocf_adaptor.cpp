/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <syslog.h>
#include <map>
#include "ctx.h"
#include "log.h"
#include "ocf_adaptor.h"

using namespace std;
static ocf_ctx_t g_ctx;
static ocf_cache_t g_cache;
static map<uint32_t, ocf_core_t> g_cores;

static int check_ocf_config(struct ocf_config *cfg)
{
	switch (cfg->cache_line_size) {
		case ocf_cache_line_size_8:
		case ocf_cache_line_size_16:
		case ocf_cache_line_size_32:
		case ocf_cache_line_size_64:
			break;
		default:
			ocf_adaptor_log(OCF_LOG_ERROR, "cache line size(%lu) is not suppoerted\n", cfg->cache_line_size);
			return STATE_FAIL;
	}

	uint128_t core_mask = cfg->core_mask;
	uint16_t num = 0;
	while (core_mask) {
		core_mask &= (core_mask - 1);
		num++;
	}

	if (num != cfg->core_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "core_num and core_mask do not match\n");
		return STATE_FAIL;
	}
	return STATE_SUCCESS;
}

static int initialize_cache(ocf_ctx_t ctx, ocf_cache_t *cache, struct ocf_config *cfg)
{
	return 0;
}

int initialize_core(ocf_cache_t cache, ocf_core_t *core)
{
	return 0;
}

int32_t ocf_init(struct ocf_config *cfg)
{
	if (cfg->log_print) {
		set_log_print(cfg->log_print);
	}

	if (check_ocf_config(cfg)) {
		return STATE_FAIL;
	}

	if (ctx_init(&g_ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf ctx init failed\n");
		return STATE_FAIL;
	}

	if (initialize_cache(g_ctx, &g_cache, cfg)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf cache init failed\n");
		ctx_cleanup(g_ctx);
		return STATE_FAIL;
	}

	return STATE_SUCCESS;
}

int32_t ocf_add_core(uint32_t slot_id)
{
	if (g_cores.find(slot_id) != g_cores.end()) {
		return STATE_CORE_EXIST;
	}

	ocf_core_t core;
	if (initialize_core(g_cache, &core)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf core init failed\n");
		return STATE_FAIL;
	}

	g_cores[slot_id] = core;
	return STATE_SUCCESS;
}

int32_t ocf_remove_core(uint32_t slot_id)
{
	if (g_cores.find(slot_id) == g_cores.end()) {
		return STATE_CORE_NOT_EXIST;
	}

	// remove core
	g_cores.erase(slot_id);
	return STATE_SUCCESS;
}

int32_t ocf_region_invalid(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_range_invalid(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_lookup(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_get(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_put(struct req_context *ctx)
{
	return STATE_SUCCESS;
}

int32_t ocf_poll(uint32_t io_worker_id, uint32_t max_num)
{
	return STATE_SUCCESS;
}