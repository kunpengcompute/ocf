/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <syslog.h>
#include <unordered_map>
#include "completion_queue.h"
#include "ctx.h"
#include "slot_info.h"
#include "log.h"
#include "ocf_adaptor.h"

using namespace std;

#define REGION_SIZE (1UL << 35)
#define MAX_QUEUE_NUM 64
#define MAX_CQ_ENTRYS 16
#define ALIGN_SIZE 4096

#define NONE         0
#define INITIALIZED  1
#define DELETING     2

extern "C" ocf_core_id_t ocf_core_get_id(ocf_core_t core);

struct ocf_adaptor_context {
	int state = NONE;
	ocf_ctx_t ctx;
	ocf_cache_t cache;

	env_rwlock table_lock = { PTHREAD_RWLOCK_INITIALIZER };
	unordered_map<uint32_t, slot_info_t> slot_info_table;
	unordered_map<uint32_t, unordered_map<uint32_t, int>> region_remap_table;
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

static void core_remove_complete(void *ctx, int ret)
{
	struct simple_context *context = (struct simple_context *)ctx;

	if (ret) {
		*context->ret = ret;
	}
	sem_post(&context->sem);
}

static void cache_remove_complete(ocf_cache_t cache, void *ctx, int ret)
{
	struct simple_context *context = (struct simple_context *)ctx;

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
	int ret;
	int op = io->dir;
	switch (error) {
		case 0:
			ret = STATE_SUCCESS;
			break;
		// case OCF_ERR_NOT_UNAVAILABLE ret = STATE_OCF_UNAVAILABLE
		default:
			ret = ((op == OCF_LOOKUP || op == OCF_READ) ? STATE_MISS : STATE_FAIL);
			break;
	}

	entry->ret = ret;
	completion_queue_push(cq, entry);

	ocf_io_put(io);
}

static int submit_io(struct req_context *ctx, ocf_core_t core,
	uint64_t addr, uint64_t len, int dir, ocf_end_io_t cmpl)
{
	ocf_cache_t cache = ocf_core_get_cache(core);
	ocf_volume_t core_vol = ocf_core_get_front_volume(core);
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(cache);
	if (ctx->io_worker_id >= priv->queue_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_work_id(%u) is not within the range of [0, %u)\n",
			ctx->io_worker_id, priv->queue_num);
		return STATE_PRRAM_INVALID;
	}

	ocf_queue_t q = priv->io_queues[ctx->io_worker_id];
	/* allocate new io */
	struct ocf_io *io = ocf_volume_new_io(core_vol, q, addr, len, dir, 0, 0);
	if (!io) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io memory request fail\n");
		return STATE_MEM_ALLOC_ERR;
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
	if (g_adaptor.state != NONE) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf has been initialized\n");
		return STATE_FAIL;
	}

	if (!cfg) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_init cfg is NULL\n");
		return STATE_PRRAM_INVALID;
	}

	if (cfg->log_print) {
		set_log_print(cfg->log_print);
	}

	if (check_ocf_config(cfg)) {
		return STATE_PRRAM_INVALID;
	}

	if (ctx_init(&g_adaptor.ctx)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf ctx init failed\n");
		return STATE_FAIL;
	}

	if (initialize_cache(g_adaptor.ctx, &g_adaptor.cache, cfg)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf cache init failed\n");
		ctx_cleanup(g_adaptor.ctx);
		return STATE_FAIL;
	}

	g_adaptor.state = INITIALIZED;
	ocf_adaptor_log(OCF_LOG_INFO, "ocf init complete\n");
	return STATE_SUCCESS;
}

void ocf_exit()
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf is not initialized, not need to delete\n");
		return;
	}
	g_adaptor.state = DELETING;

	int ret = STATE_SUCCESS;
	simple_context ctx;
	ctx.ret = &ret;
	sem_init(&ctx.sem, 0, 0);
	unordered_map<uint32_t, slot_info_t> slot_info_table;
	unordered_map<uint32_t, unordered_map<uint32_t, int>> region_remap_table;

	/* clear slot hash table */
	env_rwlock_write_lock(&g_adaptor.table_lock);
	swap(slot_info_table, g_adaptor.slot_info_table);
	swap(region_remap_table, g_adaptor.region_remap_table);
	env_rwlock_write_unlock(&g_adaptor.table_lock);

	/* Remove core from cache */
	for (auto it: slot_info_table) {
		slot_info_t info = it.second;
		ocf_mngt_cache_remove_core(info->core, core_remove_complete, &ctx);
		env_free(info);
	}
	for (uint32_t i = 0; i < slot_info_table.size(); ++i) {
		sem_wait(&ctx.sem);
	}
	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_WARN, "ocf core remove fail\n");
	}

	/* Stop cache */
	ret = STATE_SUCCESS;
	ocf_mngt_cache_stop(g_adaptor.cache, cache_remove_complete, &ctx);
	sem_wait(&ctx.sem);
	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_WARN, "ocf cache remove fail\n");
	}

	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(g_adaptor.cache);

	/* Put the management queue */
	ocf_queue_put(priv->mngt_queue);

	for (uint32_t i = 0; i < priv->queue_num; ++i) {
		completion_queue_put(priv->completion_queues[i], 1);
	}

	free(priv);

	/* Deinitialize context */
	ctx_cleanup(g_adaptor.ctx);

	/* Destroy completion semaphore */
	sem_destroy(&ctx.sem);

	g_adaptor.state = NONE;
	ocf_adaptor_log(OCF_LOG_INFO, "ocf exit complete\n");
}

int ocf_add_core(uint32_t slot_id)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not add core\n");
		return STATE_FAIL;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	auto &table_lock = g_adaptor.table_lock;
	slot_info_t info;
	env_rwlock_write_lock(&table_lock);
	if (slot_info_table.find(slot_id) != slot_info_table.end()) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core already exists\n", slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_CORE_EXIST;
	} else {
		info = (slot_info_t)env_zalloc(sizeof(*info), 0);
		if (!info) {
			env_rwlock_write_unlock(&table_lock);
			return STATE_MEM_ALLOC_ERR;
		}
		slot_info_table[slot_id] = info;
	}
	env_rwlock_write_unlock(&table_lock);

	ocf_core_t core;
	if (initialize_core(g_adaptor.cache, &core)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core init failed\n", slot_id);
		env_rwlock_write_lock(&table_lock);
		slot_info_table.erase(slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_FAIL;
	}
	
	env_rwlock_write_lock(&table_lock);
	info->core = core;
	region_remap_table[slot_id] = unordered_map<uint32_t, int>();
	env_rwlock_write_unlock(&table_lock);
	ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) core(%u) add success\n", slot_id, ocf_core_get_id(core));
	return STATE_SUCCESS;
}

int ocf_remove_core(uint32_t slot_id)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not remove core\n");
		return STATE_FAIL;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	auto &table_lock = g_adaptor.table_lock;
	slot_info_t info;
	ocf_core_t core;
	ocf_core_id_t core_id;
	env_rwlock_write_lock(&table_lock);
	if (slot_info_table.find(slot_id) == slot_info_table.end()) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core is not exists\n", slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_CORE_NOT_EXIST;
	}
	info = slot_info_table[slot_id];
	if (!info->core) {
		ocf_adaptor_log(OCF_LOG_ERROR, "slot(%u) core is creating, can not remove\n", slot_id);
		env_rwlock_write_unlock(&table_lock);
		return STATE_CORE_CREATING;
	}
	core = info->core;
	core_id = ocf_core_get_id(core);
	slot_info_table.erase(slot_id);
	region_remap_table.erase(slot_id);
	ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) core(%u) remove success\n", slot_id, core_id);
	env_rwlock_write_unlock(&table_lock);

	/* remove core from cache */
	int ret = STATE_SUCCESS;
	simple_context ctx;
	ctx.ret = &ret;
	sem_init(&ctx.sem, 0, 0);
	ocf_mngt_cache_remove_core(core, core_remove_complete, &ctx);
	sem_wait(&ctx.sem);
	if (ret) {
		/* default deletion will not fail */
		ocf_adaptor_log(OCF_LOG_WARN, "cache remove core(%u) fail\n", core_id);
	}
	env_free(info);

	return STATE_SUCCESS;
}

int ocf_region_invalid(struct req_context *ctx)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not submit region_invalid io\n");
		return STATE_FAIL;
	}

	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_region_invalid ctx is NULL\n");
		return STATE_PRRAM_INVALID;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(ctx->slot_id) == region_remap_table.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	/* no concurrent requests for a slot from multiple threads */
	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_SUCCESS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	uint64_t core_offset = remap_id * REGION_SIZE;
	cq_entry_t entry = (cq_entry_t)ctx->internal;
	entry->is_region_invalid = 1;
	return submit_io(ctx, core, core_offset, REGION_SIZE, OCF_INVALID, complete);
}

int ocf_range_invalid(struct req_context *ctx)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not submit range_invalid io\n");
		return STATE_FAIL;
	}

	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_range_invalid ctx is NULL\n");
		return STATE_PRRAM_INVALID;
	}

	if ((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_WARN, "ocf_range_invalid is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_SUCCESS, ctx);
		}
		return STATE_SUCCESS;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(ctx->slot_id) == region_remap_table.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	/* no concurrent requests for a slot from multiple threads */
	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_SUCCESS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	uint64_t core_offset = remap_id * REGION_SIZE + ctx->offset;
	cq_entry_t entry = (cq_entry_t)ctx->internal;
	entry->is_region_invalid = 0;
	return submit_io(ctx, core, core_offset, ctx->len, OCF_INVALID, complete);
}

int ocf_lookup(struct req_context *ctx)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not submit lookup io\n");
		return STATE_FAIL;
	}

	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_lookup ctx is NULL\n");
		return STATE_PRRAM_INVALID;
	}

	if ((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ock_lookup is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(ctx->slot_id) == region_remap_table.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	/* no concurrent requests for a slot from multiple threads */
	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	uint64_t core_offset = remap_id * REGION_SIZE + ctx->offset;
	cq_entry_t entry = (cq_entry_t)ctx->internal;
	entry->is_region_invalid = 0;
	return submit_io(ctx, core, core_offset, ctx->len, OCF_LOOKUP, complete);
}

int ocf_get(struct req_context *ctx)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not submit read io\n");
		return STATE_FAIL;
	}

	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_get ctx is NULL\n");
		return STATE_PRRAM_INVALID;
	}

	if ((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_get is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(ctx->slot_id) == region_remap_table.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	/* no concurrent requests for a slot from multiple threads */
	slot_info_t info = slot_info_table[ctx->slot_id];
	auto &region_map = region_remap_table[ctx->slot_id];
	ocf_core_t core = info->core;
	if (region_map.find(ctx->region_id) == region_map.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		if (ctx->cb) {
			ctx->cb(STATE_MISS, ctx);
		}
		return STATE_SUCCESS;
	}
	uint64_t remap_id = region_map[ctx->region_id];
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	uint64_t core_offset = remap_id * REGION_SIZE + ctx->offset;
	cq_entry_t entry = (cq_entry_t)ctx->internal;
	entry->is_region_invalid = 0;
	return submit_io(ctx, core, core_offset, ctx->len, OCF_READ, complete);
}

int ocf_put(struct req_context *ctx)
{
	if (g_adaptor.state != INITIALIZED) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf is not initialized, can not submit write io\n");
		return STATE_FAIL;
	}

	if (!ctx) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_put ctx is NULL\n");
		return STATE_PRRAM_INVALID;
	}

	if ((ctx->offset % ALIGN_SIZE) || (ctx->len % ALIGN_SIZE)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "ocf_put is not 4k aligned\n");
		if (ctx->cb) {
			ctx->cb(STATE_SUCCESS, ctx);
		}
		return STATE_SUCCESS;
	}

	auto &slot_info_table = g_adaptor.slot_info_table;
	auto &region_remap_table = g_adaptor.region_remap_table;
	env_rwlock_read_lock(&g_adaptor.table_lock);
	if (region_remap_table.find(ctx->slot_id) == region_remap_table.end()) {
		env_rwlock_read_unlock(&g_adaptor.table_lock);
		return STATE_CORE_NOT_EXIST;
	}

	/* no concurrent requests for a slot from multiple threads */
	slot_info_t info = slot_info_table[ctx->slot_id];
	ocf_core_t core = info->core;
	auto &region_map = region_remap_table[ctx->slot_id];
	int remap_id;
	if (region_map.find(ctx->region_id) != region_map.end()) {
		remap_id = region_map[ctx->region_id];
	} else {
		remap_id = get_remap_id(info);
		if (remap_id < 0) {
			env_rwlock_read_unlock(&g_adaptor.table_lock);
			if (ctx->cb) {
				ctx->cb(STATE_TOO_MANY_REGION, ctx);
			}
			return STATE_SUCCESS;
		}
		region_map[ctx->region_id] = remap_id;
		ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) add region_id(%u)-remap_id(%d)\n",
			ctx->slot_id, ctx->region_id, remap_id);
	}
	env_rwlock_read_unlock(&g_adaptor.table_lock);

	uint64_t core_offset = remap_id * REGION_SIZE + ctx->offset;
	cq_entry_t entry = (cq_entry_t)ctx->internal;
	entry->is_region_invalid = 0;
	return submit_io(ctx, core, core_offset, ctx->len, OCF_WRITE, complete);
}

int ocf_poll(uint32_t io_worker_id, int max_num)
{
	struct cache_priv *priv = (struct cache_priv *)ocf_cache_get_priv(g_adaptor.cache);
	if (io_worker_id >= priv->queue_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_work_id(%u) can not exceed %u\n", io_worker_id, priv->queue_num);
		return STATE_PRRAM_INVALID;
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
	int num = completion_queue_pop(q, entrys, limit);
	struct req_context *ctx;
	for (int i = 0; i < num; ++i) {
		entry = entrys[i];
		ctx = (struct req_context *)get_req_context(entry);

		/* if region invalid success, delete region remapping key-value in the request sending thread */
		if (unlikely(entry->is_region_invalid && entry->ret == STATE_SUCCESS)) {
			auto &region_remap_table = g_adaptor.region_remap_table;
			env_rwlock_read_lock(&g_adaptor.table_lock);
			if (region_remap_table.find(ctx->slot_id) != region_remap_table.end()) {
				auto &region_remap = region_remap_table[ctx->slot_id];
				region_remap.erase(ctx->region_id);
				ocf_adaptor_log(OCF_LOG_INFO, "slot(%u) remove region_id(%u) remap\n",
					ctx->slot_id, ctx->region_id);
			}
			env_rwlock_read_unlock(&g_adaptor.table_lock);
		}

		ctx->cb(entry->ret, ctx);
	}
	return STATE_SUCCESS;
}

