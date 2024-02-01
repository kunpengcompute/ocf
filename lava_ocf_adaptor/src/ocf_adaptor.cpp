/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <syslog.h>
#include <map>
#include "completion_queue.h"
#include "ctx.h"
#include "log.h"
#include "ocf_adaptor.h"

using namespace std;

#define REGION_SIZE (1UL << 35)
#define MAX_QUEUE_NUM 64
#define MAX_CQ_ENTRYS 16

static ocf_ctx_t g_ctx;
static ocf_cache_t g_cache;
static map<uint32_t, ocf_core_t> g_cores;

struct cache_priv {
	ocf_queue_t mngt_queue;
	ocf_queue_t io_queues[MAX_QUEUE_NUM];
	completion_queue_t completion_queues[MAX_QUEUE_NUM];
	uint32_t queue_num;
};

static int check_ocf_config(struct ocf_config *cfg)
{
	if (cfg->io_worker_num > MAX_QUEUE_NUM) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_worker_num can not exceed %u\n", MAX_QUEUE_NUM);
		return STATE_FAIL;
	}

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
	// cache_priv init and queue init
	return 0;
}

static int initialize_core(ocf_cache_t cache, ocf_core_t *core)
{
	return 0;
}

static void complete(struct ocf_io *io, int error)
{
	struct req_context *ctx = (struct req_context *)io->priv1;

	ocf_cache_t cache = ocf_queue_get_cache(io->io_queue);
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(cache);
	completion_queue_t cq = priv->completion_queues[ctx->io_worker_id];

	cq_entry_t entry = (cq_entry_t)ctx->internal; 
	entry->ret = (error == 0) ? STATE_SUCCESS : STATE_FAIL;
	completion_queue_push(cq, entry);

	ocf_io_put(io);
}

static int submit_io(struct req_context *ctx,
	uint64_t addr, uint64_t len, int dir, ocf_end_io_t cmpl)
{
	if (g_cores.find(ctx->slot_id) == g_cores.end()) {
		return STATE_CORE_NOT_EXIST;
	}

	ocf_core_t core = g_cores[ctx->slot_id];
	ocf_cache_t cache = ocf_core_get_cache(core);
	ocf_volume_t core_vol = ocf_core_get_front_volume(core);
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(cache);
	if (ctx->io_worker_id >= priv->queue_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_work_id(%u) is not within the range of [0, %u)\n",
			ctx->io_worker_id, priv->queue_num);
		return STATE_FAIL;
	}

	ocf_queue_t q = priv->io_queues[ctx->io_worker_id];
	/* allocate new io */
	struct ocf_io *io = ocf_volume_new_io(core_vol, q, addr, len, dir, 0, 0);
	if (!io) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io memory request fail\n");
		return STATE_FAIL;
	}

	/* assign data to io, used when read/write, unused when lookup/invalid */
	ocf_io_set_data(io, ctx->buffer, 0);
	/* setup completion function */
	ocf_io_set_cmpl(io, ctx, NULL, cmpl);
	/* submit io */
	ocf_core_submit_io(io);

	return STATE_SUCCESS;
}

int ocf_init(struct ocf_config *cfg)
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

int ocf_add_core(uint32_t slot_id)
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

int ocf_remove_core(uint32_t slot_id)
{
	if (g_cores.find(slot_id) == g_cores.end()) {
		return STATE_CORE_NOT_EXIST;
	}

	// remove core
	g_cores.erase(slot_id);
	return STATE_SUCCESS;
}

int ocf_region_invalid(struct req_context *ctx)
{
	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_region_invalid ctx is NULL\n");
		return STATE_FAIL;
	}

	uint64_t core_offset = ctx->region_id * REGION_SIZE;
	return submit_io(ctx, core_offset, REGION_SIZE, OCF_INVALID, complete);
}

int ocf_range_invalid(struct req_context *ctx)
{
	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_range_invalid ctx is NULL\n");
		return STATE_FAIL;
	}

	uint64_t core_offset = ctx->region_id * REGION_SIZE + ctx->offset;
	return submit_io(ctx, core_offset, ctx->len, OCF_INVALID, complete);
}

int ocf_lookup(struct req_context *ctx)
{
	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_lookup ctx is NULL\n");
		return STATE_FAIL;
	}

	uint64_t core_offset = ctx->region_id * REGION_SIZE + ctx->offset;
	return submit_io(ctx, core_offset, ctx->len, OCF_LOOKUP, complete);
}

int ocf_get(struct req_context *ctx)
{
	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_get ctx is NULL\n");
		return STATE_FAIL;
	}

	uint64_t core_offset = ctx->region_id * REGION_SIZE + ctx->offset;
	return submit_io(ctx, core_offset, ctx->len, OCF_READ, complete);
}

int ocf_put(struct req_context *ctx)
{
	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_put ctx is NULL\n");
		return STATE_FAIL;
	}

	uint64_t core_offset = ctx->region_id * REGION_SIZE + ctx->offset;
	return submit_io(ctx, core_offset, ctx->len, OCF_WRITE, complete);
}

int ocf_poll(uint32_t io_worker_id, int max_num)
{
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(g_cache);
	if (io_worker_id >= priv->queue_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_work_id(%u) can not exceed %u\n", io_worker_id, priv->queue_num);
		return STATE_FAIL;
	}

	int limit;
	if (!max_num) {
		limit = MAX_CQ_ENTRYS;
	} else {
		limit = (max_num > MAX_CQ_ENTRYS) ? MAX_CQ_ENTRYS : max_num;
	}

	completion_queue_t q = priv->completion_queues[io_worker_id];
	cq_entry_t entrys[MAX_CQ_ENTRYS];
	cq_entry_t entry;
	int num = completion_queue_pop_batch(q, entrys, limit);
	struct req_context *ctx;
	for (int i = 0; i < num; ++i) {
		entry = entrys[i];
		ctx = (struct req_context *)get_req_context(entry);
		ctx->cb(entry->ret, ctx);
	}
	return STATE_SUCCESS;
}