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
#define ALIGN_SIZE 4096

#define OCF_ADAPTOR_STATE_NONE         0
#define OCF_ADAPTOR_STATE_INITIALIZED  1
#define OCF_ADAPTOR_STATE_INITIALIZING 2
#define OCF_ADAPTOR_STATE_DELETING     3

struct ocf_adaptor_context {
	env_rwlock lock = { PTHREAD_RWLOCK_INITIALIZED };
	int state = OCF_ADAPTOR_STATE_NONE;
	ocf_ctx_t ctx;
	ocf_cache_t cache;

	env_rwlock core_lock = { PTHREAD_RWLOCK_INITIALIZED };
	map<uint32_t, ocf_core_t> cores;
} g_adaptor;

struct cache_priv {
	ocf_queue_t mngt_queue;
	ocf_queue_t io_queues[MAX_QUEUE_NUM];
	completion_queue_t completion_queues[MAX_QUEUE_NUM];
	uint32_t queue_num;
};

struct simple_context {
	sem_t sem;
	int *ret;
};

static int check_ocf_config(struct ocf_config *cfg)
{
	if (cfg->io_worker_num > MAX_QUEUE_NUM) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_worker_num can not exceed %u\n", MAX_QUEUE_NUM);
		return STATE_FAIL;
	}

	if ((cfg->offset % ALIGN_SIZE) || (cfg->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "the access interval is not 4k aligned\n");
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

static void simple_complete(void *priv, int ret)
{
	struct simple_context *context = priv;

	if (ret) {
		*context->ret = ret;
	}
	sem_post(&context->sem);
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
	env_rwlock_write_lock(&g_adaptor.lock);
	if (g_adaptor.state != OCF_ADAPTOR_STATE_NONE) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf has been initialized\n");
		env_rwlock_write_unlock(&g_adaptor.lock);
		return STATE_MULTI_INIT;
	}

	g_adaptor.state = OCF_ADAPTOR_STATE_INITIALIZING;
	env_rwlock_write_unlock(&g_adaptor.lock);
	int ret = STATE_SUCCESS;

	if (!cfg) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_init cfg is NULL\n");
		ret = STATE_PRRAM_INVALID;
		goto err;
	}

	if (cfg->log_print) {
		set_log_print(cfg->log_print);
	}

	if (check_ocf_config(cfg)) {
		ret = STATE_PRRAM_INVALID;
		goto err;
	}

	if (ctx_init(&g_adaptor.ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf ctx init failed\n");
		ret = STATE_FAIL;
		goto err;
	}

	if (initialize_cache(g_adaptor.ctx, &g_adaptor.cache, cfg)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf cache init failed\n");
		ctx_cleanup(g_adaptor.ctx);
		ret = STATE_FAIL;
		goto err;
	}

	env_rwlock_write_lock(&g_adaptor.lock);
	g_adaptor.state = OCF_ADAPTOR_STATE_INITIALIZED;
	env_rwlock_write_unlock(&g_adaptor.lock);
	return STATE_SUCCESS;
err:
	env_rwlock_write_lock(&g_adaptor.lock);
	g_adaptor.state = OCF_ADAPTOR_STATE_NONE;
	env_rwlock_write_unlock(&g_adaptor.lock);
	return ret;
}

void ocf_exit()
{
	env_rwlock_write_lock(&g_adaptor.lock);
	if (g_adaptor.state != OCF_ADAPTOR_STATE_INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf is not initialized, not need to be deleted\n");
		env_rwlock_write_unlock(&g_adaptor.lock);
		return;
	}
	g_adaptor.state = OCF_ADAPTOR_STATE_DELETING;
	env_rwlock_write_unlock(&g_adaptor.lock);

	int ret;
	simple_context ctx;
	ctx.ret = &ret;
	sem_init(&context.sem, 0, 0);
	map<uint32_t, ocf_core_t> cores;

	env_rwlock_write_lock(&g_adaptor.core_lock);
	swap(cores, g_adaptor.cores);
	env_rwlock_write_unlock(&g_adaptor.core_lock);

	/* Remove core from cache */
	ret = STATE_SUCCESS;
	for (auto it: cores) {
		ocf_core_t core = it.second;
		ocf_mngt_cache_remove_core(core, remove_core_complete, &ctx);
	}
	for (int i = 0; i < mp.size(); ++i) {
		sem_wait(&cxt.sem);
	}
	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_WARN, "ocf core remove fail\n");
	}

	/* Stop cache */
	ret = STATE_SUCCESS;
	ocf_mngt_cache_stop(g_adaptor.cache, simple_complete, &ctx);
	sem_wait(&ctx.sem);
	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_WARN, "ocf cache remove fail\n");
	}

	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(g_adaptor.cache);

	/* Put the management queue */
	ocf_queue_put(priv->mngt_queue);

	for (int i = 0; i < priv->queue_num; ++i) {
		completion_queue_put(priv->completion_queues[i]);
	}

	free(priv);

	/* Deinitialize context */
	ctx_cleanup(ctx);

	/* Destroy completion semaphore */
	sem_destroy(&context.sem);

	env_rwlock_write_lock(&g_adaptor.lock);
	g_adaptor.state = OCF_ADAPTOR_STATE_NONE;
	env_rwlock_write_unlock(&g_adaptor.lock);
}

int ocf_add_core(uint32_t slot_id)
{
	env_rwlock_read_lock(&g_adaptor.lock);
	if (g_adaptor.state != OCF_ADAPTOR_STATE_INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not add core\n");
		env_rwlock_read_unlock(&g_adaptor.lock);
		return STATE_FAIL;
	}
	env_rwlock_read_unlock(&g_adaptor.lock);

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

