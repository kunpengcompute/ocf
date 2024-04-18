/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ocf/ocf.h"
#include "ocf_priv.h"
#include "metadata/metadata.h"
#include "engine/cache_engine.h"
#include "utils/utils_user_part.h"
#include "utils/utils_cache_line.h"
#include "utils/utils_stats.h"
#include "utils/utils_strbuf.h"

static void _fill_req(struct ocf_stats_requests *req, struct ocf_stats_core *s)
{
	uint64_t serviced = s->read_reqs.total + s->write_reqs.total +
			s->lookup_reqs.total + s->invalid_reqs.total;
	uint64_t total = serviced + s->read_reqs.pass_through +
			s->write_reqs.pass_through;
	uint64_t hit;

	/* Reads Section */
	hit = s->read_reqs.total - (s->read_reqs.full_miss +
			s->read_reqs.partial_miss);
	_set(&req->rd_hits, hit, total);
	_set(&req->rd_partial_misses, s->read_reqs.partial_miss, total);
	_set(&req->rd_full_misses, s->read_reqs.full_miss, total);
	_set(&req->rd_total, s->read_reqs.total, total);

	/* Write Section */
	hit = s->write_reqs.total - (s->write_reqs.full_miss +
					s->write_reqs.partial_miss);
	_set(&req->wr_hits, hit, total);
	_set(&req->wr_partial_misses, s->write_reqs.partial_miss, total);
	_set(&req->wr_full_misses, s->write_reqs.full_miss, total);
	_set(&req->wr_total, s->write_reqs.total, total);

	/* Pass-Through section */
	_set(&req->rd_pt, s->read_reqs.pass_through, total);
	_set(&req->wr_pt, s->write_reqs.pass_through, total);

	/* Lookup section */
	hit = s->lookup_reqs.total - (s->lookup_reqs.full_miss +
					s->lookup_reqs.partial_miss);
	_set(&req->lookup_hits, hit, total);
	_set(&req->lookup_partial_misses, s->lookup_reqs.partial_miss, total);
	_set(&req->lookup_full_misses, s->lookup_reqs.full_miss, total);
	_set(&req->lookup_total, s->lookup_reqs.total, total);

	/* Invalid section */
	_set(&req->invalid_total, s->invalid_reqs.total, total);

	/* Summary */
	_set(&req->serviced, serviced, total);
	_set(&req->total, total, total);
}

static void _fill_req_part(struct ocf_stats_requests *req,
		struct ocf_stats_io_class *s)
{
	uint64_t serviced = s->read_reqs.total + s->write_reqs.total;
	uint64_t total = serviced + s->read_reqs.pass_through +
			s->write_reqs.pass_through;
	uint64_t hit;

	/* Reads Section */
	hit = s->read_reqs.total - (s->read_reqs.full_miss +
			s->read_reqs.partial_miss);
	_set(&req->rd_hits, hit, total);
	_set(&req->rd_partial_misses, s->read_reqs.partial_miss, total);
	_set(&req->rd_full_misses, s->read_reqs.full_miss, total);
	_set(&req->rd_total, s->read_reqs.total, total);

	/* Write Section */
	hit = s->write_reqs.total - (s->write_reqs.full_miss +
					s->write_reqs.partial_miss);
	_set(&req->wr_hits, hit, total);
	_set(&req->wr_partial_misses, s->write_reqs.partial_miss, total);
	_set(&req->wr_full_misses, s->write_reqs.full_miss, total);
	_set(&req->wr_total, s->write_reqs.total, total);

	/* Pass-Through section */
	_set(&req->rd_pt, s->read_reqs.pass_through, total);
	_set(&req->wr_pt, s->write_reqs.pass_through, total);

	/* Summary */
	_set(&req->serviced, serviced, total);
	_set(&req->total, total, total);
}

static void _fill_blocks(struct ocf_stats_blocks *blocks,
		struct ocf_stats_core *s)
{
	uint64_t rd, wr, total;

	/* Core volume */
	rd = _bytes4k(s->core_volume.read);
	wr = _bytes4k(s->core_volume.write);
	total = rd + wr;
	_set(&blocks->core_volume_rd, rd, total);
	_set(&blocks->core_volume_wr, wr, total);
	_set(&blocks->core_volume_total, total, total);

	/* Cache volume */
	rd = _bytes4k(s->cache_volume.read);
	wr = _bytes4k(s->cache_volume.write);
	total = rd + wr;
	_set(&blocks->cache_volume_rd, rd, total);
	_set(&blocks->cache_volume_wr, wr, total);
	_set(&blocks->cache_volume_total, total, total);

	/* Core (cache volume) */
	rd = _bytes4k(s->core.read);
	wr = _bytes4k(s->core.write);
	total = rd + wr;
	_set(&blocks->volume_rd, rd, total);
	_set(&blocks->volume_wr, wr, total);
	_set(&blocks->volume_total, total, total);
}

static void _fill_blocks_part(struct ocf_stats_blocks *blocks,
		struct ocf_stats_io_class *s)
{
	uint64_t rd, wr, total;

	/* Core volume */
	rd = _bytes4k(s->core_blocks.read);
	wr = _bytes4k(s->core_blocks.write);
	total = rd + wr;
	_set(&blocks->core_volume_rd, rd, total);
	_set(&blocks->core_volume_wr, wr, total);
	_set(&blocks->core_volume_total, total, total);

	/* Cache volume */
	rd = _bytes4k(s->cache_blocks.read);
	wr = _bytes4k(s->cache_blocks.write);
	total = rd + wr;
	_set(&blocks->cache_volume_rd, rd, total);
	_set(&blocks->cache_volume_wr, wr, total);
	_set(&blocks->cache_volume_total, total, total);

	/* Core (cache volume) */
	rd = _bytes4k(s->blocks.read);
	wr = _bytes4k(s->blocks.write);
	total = rd + wr;
	_set(&blocks->volume_rd, rd, total);
	_set(&blocks->volume_wr, wr, total);
	_set(&blocks->volume_total, total, total);
}

static void _fill_errors(struct ocf_stats_errors *errors,
		struct ocf_stats_core *s)
{
	uint64_t rd, wr, lookup, invalid, total;

	rd = s->core_errors.read;
	wr = s->core_errors.write;
	total = rd + wr;
	_set(&errors->core_volume_rd, rd, total);
	_set(&errors->core_volume_wr, wr, total);
	_set(&errors->core_volume_total, total, total);

	rd = s->cache_errors.read;
	wr = s->cache_errors.write;
	total = rd + wr;
	_set(&errors->cache_volume_rd, rd, total);
	_set(&errors->cache_volume_wr, wr, total);
	_set(&errors->cache_volume_total, total, total);

	/** place where OCF collects error IO needs to exclude backend errors */
	if (s->ocf_errors[STATS_TYPE_READ].total
		<= (s->core_errors.read + s->cache_errors.read)) {
		rd = 0;
	} else {
		rd = s->ocf_errors[STATS_TYPE_READ].total - 
				(s->core_errors.read + s->cache_errors.read);
	}
	if (s->ocf_errors[STATS_TYPE_WRITE].total
		<= (s->core_errors.write + s->cache_errors.write)) {
		wr = 0;
	} else {
		wr = s->ocf_errors[STATS_TYPE_WRITE].total - 
				(s->core_errors.write + s->cache_errors.write);
	}
	lookup = s->ocf_errors[STATS_TYPE_LOOKUP].total;
	invalid = s->ocf_errors[STATS_TYPE_INVALID].total;
	total = rd + wr + lookup + invalid;
	_set(&errors->ocf_rd, rd, total);
	_set(&errors->ocf_wr, wr, total);
	_set(&errors->ocf_lookup, lookup, total);
	_set(&errors->ocf_invalid, invalid, total);
	_set(&errors->ocf_total, total, total);

	total += s->core_errors.read + s->core_errors.write +
		s->cache_errors.read + s->cache_errors.write;

	_set(&errors->total, total, total);
}

static void _fill_success(struct ocf_stats_success *success,
		struct ocf_stats_core *s)
{
	uint64_t rd, wr, total;

	rd = s->core_success.read;
	wr = s->core_success.write;
	total = rd + wr;
	_set(&success->core_volume_rd, rd, total);
	_set(&success->core_volume_wr, wr, total);
	_set(&success->core_volume_total, total, total);

	rd = s->cache_success.read;
	wr = s->cache_success.write;
	total = rd + wr;
	_set(&success->cache_volume_rd, rd, total);
	_set(&success->cache_volume_wr, wr, total);
	_set(&success->cache_volume_total, total, total);

	total = s->core_success.read + s->core_success.write +
		s->cache_success.read + s->cache_success.write;

	_set(&success->total, total, total);
}

static void _fill_latencys(struct ocf_stats_latencys *latencys,
		struct ocf_stats_core *s)
{
	for (int i = 0; i<STATS_TYPE_MAX; i++) {
		/* set ocf latency */
		_set(&latencys->ocf_latency_items[i].max, s->ocf_latency[i].max, 0);
		if (likely(s->ocf_latency[i].min != UINT64_MAX)) {
			_set(&latencys->ocf_latency_items[i].min, s->ocf_latency[i].min, 0);
		}
		_set(&latencys->ocf_latency_items[i].samples, s->ocf_latency[i].samples, 0);
		_set_double(&latencys->ocf_latency_items[i].avg, s->ocf_latency[i].avg, 0);

		/* set backend latency*/
		_set(&latencys->backend_latency_items[i].max, s->backend_latency[i].max, 0);
		if (likely(s->backend_latency[i].min != UINT64_MAX)) {
			_set(&latencys->backend_latency_items[i].min, s->backend_latency[i].min, 0);
		}
		_set(&latencys->backend_latency_items[i].samples, s->backend_latency[i].samples, 0);
		_set_double(&latencys->backend_latency_items[i].avg, s->backend_latency[i].avg, 0);
	}
}

static void _fill_ocf_inout_reqs(struct ocf_stats_ocf_inout_reqs *frequencys,
		struct ocf_stats_core *s)
{
	uint64_t total_in = 0, total_out = 0;
	for (int i = 0; i<STATS_TYPE_MAX; i++) {
		total_in += s->ocf_in_reqs[i].total;
		total_out += s->ocf_out_reqs[i].total;
	}
	_set(&frequencys->total_in, total_in, total_in);
	_set(&frequencys->total_out, total_out, total_out);

	for (int i = 0; i<STATS_TYPE_MAX; i++) {
		_set(&frequencys->in_reqs[i], s->ocf_in_reqs[i].total, total_in);
		_set(&frequencys->out_reqs[i], s->ocf_out_reqs[i].total, total_out);
	}
}

static void _accumulate_block(struct ocf_stats_block *to,
		const struct ocf_stats_block *from)
{
	to->read += from->read;
	to->write += from->write;
}

static void _accumulate_reqs(struct ocf_stats_req *to,
		const struct ocf_stats_req *from)
{
	to->full_miss += from->full_miss;
	to->partial_miss += from->partial_miss;
	to->total += from->total;
	to->pass_through += from->pass_through;
}

static void _accumulate_rw(struct ocf_stats_rw *to,
		const struct ocf_stats_rw *from)
{
	to->read += from->read;
	to->write += from->write;
}

static void _accumulate_latency(struct ocf_stats_latency to[],
		const struct ocf_stats_latency from[], int size)
{
	for (int i = 0; i<size; i++) {
		if (from[i].max > to[i].max)
			to[i].max = from[i].max;
		if (from[i].min < to[i].min)
			to[i].min = from[i].min;

		if (to[i].samples != 0) {
			to[i].avg = ((to[i].avg)*(to[i].samples)+(from[i].avg)*(from[i].samples))
					/ (to[i].samples+from[i].samples);
			to[i].samples += from[i].samples;
		} else {
			to[i].avg = from[i].avg;
			to[i].samples = from[i].samples;
		}
	}
}

static inline void _accumulate_frequency(struct ocf_stats_frequency *to,
		const struct ocf_stats_frequency *from)
{
	to->total += from->total;
}

static void _accumulate_frequencys(struct ocf_stats_frequency to[],
		const struct ocf_stats_frequency from[], int size)
{
	for (int i = 0; i<size; i++) {
		_accumulate_frequency(&to[i], &from[i]);
	}
}

struct io_class_stats_context {
	struct ocf_stats_io_class *stats;
	ocf_part_id_t part_id;
};

static int _accumulate_io_class_stats(ocf_core_t core, void *cntx)
{
	int result;
	struct ocf_stats_io_class stats;
	struct ocf_stats_io_class *total =
		((struct io_class_stats_context*)cntx)->stats;
	ocf_part_id_t part_id = ((struct io_class_stats_context*)cntx)->part_id;

	result = ocf_core_io_class_get_stats(core, part_id, &stats);
	if (result)
		return result;

	total->occupancy_clines += stats.occupancy_clines;
	total->dirty_clines += stats.dirty_clines;
	total->free_clines = stats.free_clines;

	_accumulate_block(&total->cache_blocks, &stats.cache_blocks);
	_accumulate_block(&total->core_blocks, &stats.core_blocks);
	_accumulate_block(&total->blocks, &stats.blocks);

	_accumulate_reqs(&total->read_reqs, &stats.read_reqs);
	_accumulate_reqs(&total->write_reqs, &stats.write_reqs);

	return 0;
}

static void _ocf_stats_part_fill(ocf_cache_t cache, ocf_part_id_t part_id,
		struct ocf_stats_io_class *stats , struct ocf_stats_usage *usage,
		struct ocf_stats_requests *req, struct ocf_stats_blocks *blocks)
{
	uint64_t cache_size, cache_line_size;

	cache_line_size = ocf_cache_get_line_size(cache);
	cache_size = cache->conf_meta->cachelines;

	if (usage) {
		_set(&usage->occupancy,
			_lines4k(stats->occupancy_clines, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->free,
			_lines4k(stats->free_clines, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->clean,
			_lines4k(stats->occupancy_clines - stats->dirty_clines,
				cache_line_size),
			_lines4k(stats->occupancy_clines, cache_line_size));

		_set(&usage->dirty,
			_lines4k(stats->dirty_clines, cache_line_size),
			_lines4k(stats->occupancy_clines, cache_line_size));
	}

	if (req)
		_fill_req_part(req, stats);

	if (blocks)
		_fill_blocks_part(blocks, stats);
}

int ocf_stats_collect_part_core(ocf_core_t core, ocf_part_id_t part_id,
		struct ocf_stats_usage *usage, struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks)
{
	struct ocf_stats_io_class s;
	ocf_cache_t cache;
	int result = 0;

	OCF_CHECK_NULL(core);

	cache = ocf_core_get_cache(core);

	if (ocf_cache_is_standby(cache))
		return -OCF_ERR_CACHE_STANDBY;

	if (part_id > OCF_IO_CLASS_ID_MAX)
		return -OCF_ERR_INVAL;

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);

	result = ocf_core_io_class_get_stats(core, part_id, &s);
	if (result)
		return result;

	_ocf_stats_part_fill(cache, part_id, &s, usage, req, blocks);

	return result;
}

int ocf_stats_collect_part_cache(ocf_cache_t cache, ocf_part_id_t part_id,
		struct ocf_stats_usage *usage, struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks)
{
	struct io_class_stats_context ctx;
	struct ocf_stats_io_class s = {};
	int result = 0;

	OCF_CHECK_NULL(cache);

	if (ocf_cache_is_standby(cache))
		return -OCF_ERR_CACHE_STANDBY;

	if (part_id > OCF_IO_CLASS_ID_MAX)
		return -OCF_ERR_INVAL;

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);

	ctx.part_id = part_id;
	ctx.stats = &s;

	result = ocf_core_visit(cache, _accumulate_io_class_stats, &ctx, true);
	if (result)
		return result;

	_ocf_stats_part_fill(cache, part_id, &s, usage, req, blocks);

	return result;
}

int ocf_stats_collect_core(ocf_core_t core,
		struct ocf_stats_usage *usage,
		struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks,
		struct ocf_stats_errors *errors,
		struct ocf_stats_success *success,
		struct ocf_stats_latencys *latencys,
		struct ocf_stats_ocf_inout_reqs *inout_reqs)
{
	ocf_cache_t cache;
	uint64_t cache_occupancy, cache_size, cache_line_size;
	struct ocf_stats_core s;
	int result;

	OCF_CHECK_NULL(core);

	cache = ocf_core_get_cache(core);

	if (ocf_cache_is_standby(cache))
		return -OCF_ERR_CACHE_STANDBY;

	result = ocf_core_get_stats(core, &s);
	if (result)
		return result;

	cache_line_size = ocf_cache_get_line_size(cache);
	cache_size = cache->conf_meta->cachelines;
	cache_occupancy = ocf_get_cache_occupancy(cache);

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);
	_ocf_stats_zero(errors);
	_ocf_stats_zero(success);
	ENV_BUG_ON(env_memset(latencys, sizeof(*latencys), 0));
	ENV_BUG_ON(env_memset(inout_reqs, sizeof(*inout_reqs), 0));

	if (usage) {
		_set(&usage->occupancy,
			_lines4k(s.cache_occupancy, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->free,
			_lines4k(cache_size - cache_occupancy, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->clean,
			_lines4k(s.cache_occupancy - s.dirty, cache_line_size),
			_lines4k(s.cache_occupancy, cache_line_size));

		_set(&usage->dirty,
			_lines4k(s.dirty, cache_line_size),
			_lines4k(s.cache_occupancy, cache_line_size));
	}

	if (req)
		_fill_req(req, &s);

	if (blocks)
		_fill_blocks(blocks, &s);

	if (errors)
		_fill_errors(errors, &s);
		
	if (success)
		_fill_success(success, &s);

	if (latencys)
		_fill_latencys(latencys, &s);

	if (inout_reqs)
		_fill_ocf_inout_reqs(inout_reqs, &s);

	return 0;
}

static int _accumulate_stats(ocf_core_t core, void *cntx)
{
	struct ocf_stats_core stats, *total = cntx;
	int result;

	result = ocf_core_get_stats(core, &stats);
	if (result)
		return result;

	_accumulate_block(&total->cache_volume, &stats.cache_volume);
	_accumulate_block(&total->core_volume, &stats.core_volume);
	_accumulate_block(&total->core, &stats.core);

	_accumulate_reqs(&total->read_reqs, &stats.read_reqs);
	_accumulate_reqs(&total->write_reqs, &stats.write_reqs);
	_accumulate_reqs(&total->lookup_reqs, &stats.lookup_reqs);
	_accumulate_frequency(&total->invalid_reqs, &stats.invalid_reqs);

	_accumulate_rw(&total->cache_errors, &stats.cache_errors);
	_accumulate_rw(&total->core_errors, &stats.core_errors);
	_accumulate_rw(&total->cache_success, &stats.cache_success);
	_accumulate_rw(&total->core_success, &stats.core_success);
	_accumulate_frequencys(total->ocf_errors, stats.ocf_errors, STATS_TYPE_MAX);

	_accumulate_latency(total->ocf_latency, stats.ocf_latency, STATS_TYPE_MAX);
	_accumulate_latency(total->backend_latency, stats.backend_latency, STATS_TYPE_MAX);

	_accumulate_frequencys(total->ocf_in_reqs, stats.ocf_in_reqs, STATS_TYPE_MAX);
	_accumulate_frequencys(total->ocf_out_reqs, stats.ocf_out_reqs, STATS_TYPE_MAX);

	return 0;
}

int ocf_stats_collect_cache(ocf_cache_t cache,
		struct ocf_stats_usage *usage,
		struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks,
		struct ocf_stats_errors *errors,
		struct ocf_stats_success *success,
		struct ocf_stats_latencys *latencys,
		struct ocf_stats_ocf_inout_reqs *inout_reqs)
{
	uint64_t cache_line_size;
	struct ocf_cache_info info;
	struct ocf_stats_core s = { 0 };
	int result;

	OCF_CHECK_NULL(cache);

	if (ocf_cache_is_standby(cache))
		return -OCF_ERR_CACHE_STANDBY;

	result = ocf_cache_get_info(cache, &info);
	if (result)
		return result;

	cache_line_size = ocf_cache_get_line_size(cache);

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);
	_ocf_stats_zero(errors);
	_ocf_stats_zero(success);
	ENV_BUG_ON(env_memset(latencys, sizeof(*latencys), 0));
	ENV_BUG_ON(env_memset(inout_reqs, sizeof(*inout_reqs), 0));
	/* set to uint64 for easy calculation of the minimum value */
	_reset_latency_min_value(&s);

	result = ocf_core_visit(cache, _accumulate_stats, &s, true);
	if (result)
		return result;

	if (usage) {
		_set(&usage->occupancy,
			_lines4k(info.occupancy, cache_line_size),
			_lines4k(info.size, cache_line_size));

		_set(&usage->free,
			_lines4k(info.size - info.occupancy, cache_line_size),
			_lines4k(info.size, cache_line_size));

		_set(&usage->clean,
			_lines4k(info.occupancy - info.dirty, cache_line_size),
			_lines4k(info.size, cache_line_size));

		_set(&usage->dirty,
			_lines4k(info.dirty, cache_line_size),
			_lines4k(info.size, cache_line_size));
	}

	if (req)
		_fill_req(req, &s);

	if (blocks)
		_fill_blocks(blocks, &s);

	if (errors)
		_fill_errors(errors, &s);
	
	if (success)
		_fill_success(success, &s);

	if (latencys)
		_fill_latencys(latencys, &s);
	
	if (inout_reqs)
		_fill_ocf_inout_reqs(inout_reqs, &s);

	return 0;
}

struct stats_dump_ctx {
	struct strbuf *buf;
	env_completion *cmpl;
};

#define STATS_DUMP_FIELD(buf, name, field, units) \
	strbuf_write_format_str(buf, name" %20lu | %3lu.%-2lu "units"\n", \
				field.value, field.fraction / 100, field.fraction % 100)

#define STATS_DUMP_FIELD_WITHOUT_F(buf, name, field, units) \
	strbuf_write_format_str(buf, name" %20lu | - "units"\n", \
				field.value)

#define STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, name, field, units) \
	do { \
		if (using_scientific_notation(field.value)) { \
			strbuf_write_format_str(buf, name" %20.2e | - "units"\n", \
						field.value); \
		} else { \
			strbuf_write_format_str(buf, name" %20.2f | - "units"\n", \
						field.value); \
		} \
	} while (0)

static void _stats_dump_usage(struct strbuf *buf, struct ocf_stats_usage *usage)
{
	strbuf_write_str(buf, "+------------------+----------------------+--------+-------------+\n");
	strbuf_write_str(buf, "| Usage statistics |         Count        |    %   |    Units    |\n");
	strbuf_write_str(buf, "+------------------+----------------------+--------+-------------+\n");
	STATS_DUMP_FIELD(buf, "| Occupancy        |", usage->occupancy, "| 4KiB blocks |");
	STATS_DUMP_FIELD(buf, "| Free             |", usage->free, "| 4KiB blocks |");
	STATS_DUMP_FIELD(buf, "| Clean            |", usage->clean, "| 4KiB blocks |");
	STATS_DUMP_FIELD(buf, "| Dirty            |", usage->dirty, "| 4KiB blocks |");
	strbuf_write_str(buf, "+------------------+----------------------+--------+-------------+\n\n");
}

static void _stats_dump_req(struct strbuf *buf, struct ocf_stats_requests *req)
{
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n");
	strbuf_write_str(buf, "| Request statistics   |         Count        |    %   |   Units  |\n");
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Read hits            |", req->rd_hits, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Read partial misses  |", req->rd_partial_misses, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Read full misses     |", req->rd_full_misses, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Read total           |", req->rd_total, "| Requests |");
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Write hits           |", req->wr_hits, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Write partial misses |", req->wr_partial_misses, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Write full misses    |", req->wr_full_misses, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Write total          |", req->wr_total, "| Requests |");
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Lookup hits          |", req->lookup_hits, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Lookup partial misses|", req->lookup_partial_misses, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Lookup full misses   |", req->lookup_full_misses, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Lookup total         |", req->lookup_total, "| Requests |");
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Invalid requests     |", req->invalid_total, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Pass-Through reads   |", req->rd_pt, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Pass-Through writes  |", req->wr_pt, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Serviced requests    |", req->serviced, "| Requests |");
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Total requests       |", req->total, "| Requests |");
	strbuf_write_str(buf, "+----------------------+----------------------+--------+----------+\n\n");
}

static void _stats_dump_blocks(struct strbuf *buf, struct ocf_stats_blocks *blocks)
{
	strbuf_write_str(buf, "+------------------------------+----------------------+--------+-------------+\n");
	strbuf_write_str(buf, "| Block statistics             |         Count        |    %   |    Units    |\n");
	strbuf_write_str(buf, "+------------------------------+----------------------+--------+-------------+\n");
	STATS_DUMP_FIELD(buf, "| Reads from cache volume      |", blocks->cache_volume_rd, "| 4KiB blocks |");
	STATS_DUMP_FIELD(buf, "| Writes to cache volume       |", blocks->cache_volume_wr, "| 4KiB blocks |");
	STATS_DUMP_FIELD(buf, "| Total to/from cache volume   |", blocks->cache_volume_total, "| 4KiB blocks |");
	strbuf_write_str(buf, "+------------------------------+----------------------+--------+-------------+\n\n");
}

static void _stats_dump_errors(struct strbuf *buf, struct ocf_stats_errors *errors)
{
	strbuf_write_str(buf, "+--------------------+----------------------+--------+----------+\n");
	strbuf_write_str(buf, "| Error statistics   |         Count        |    %   |   Units  |\n");
	strbuf_write_str(buf, "+--------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Cache read errors  |", errors->cache_volume_rd, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Cache write errors |", errors->cache_volume_wr, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Cache total errors |", errors->cache_volume_total, "| Requests |");
	strbuf_write_str(buf, "+--------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| OCF read errors    |", errors->ocf_rd, "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF write errors   |", errors->ocf_wr, "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF lookup errors  |", errors->ocf_lookup, "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF invalid errors |", errors->ocf_invalid, "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF total errors   |", errors->ocf_total, "| Requests |");
	strbuf_write_str(buf, "+--------------------+----------------------+--------+----------+\n\n");
}

static void _stats_dump_success(struct strbuf *buf, struct ocf_stats_success *success)
{
	strbuf_write_str(buf, "+---------------------+----------------------+--------+----------+\n");
	strbuf_write_str(buf, "| Success statistics  |         Count        |    %   |   Units  |\n");
	strbuf_write_str(buf, "+---------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| Cache read success  |", success->cache_volume_rd, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Cache write success |", success->cache_volume_wr, "| Requests |");
	STATS_DUMP_FIELD(buf, "| Cache total success |", success->cache_volume_total, "| Requests |");
	strbuf_write_str(buf, "+---------------------+----------------------+--------+----------+\n\n");
}

static void _stats_dump_latencys(struct strbuf *buf, struct ocf_stats_latencys *latencys)
{
	struct ocf_stats_latency_item *item = NULL;
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	strbuf_write_str(buf, "| Latency statistics      |         Count        | % |    Units    |\n");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	item = &(latencys->ocf_latency_items[STATS_TYPE_READ]);
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Read max latency    |", (*item).max, "| Microsecond |");
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Read min latency    |", (*item).min, "| Microsecond |");
	STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, "| OCF Read avg latency    |", (*item).avg, "| Microsecond |");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	item = &(latencys->ocf_latency_items[STATS_TYPE_WRITE]);
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Write max latency   |", (*item).max, "| Microsecond |");
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Write min latency   |", (*item).min, "| Microsecond |");
	STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, "| OCF Write avg latency   |", (*item).avg, "| Microsecond |");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	item = &(latencys->ocf_latency_items[STATS_TYPE_LOOKUP]);
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Lookup max latency  |", (*item).max, "| Microsecond |");
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Lookup min latency  |", (*item).min, "| Microsecond |");
	STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, "| OCF Lookup avg latency  |", (*item).avg, "| Microsecond |");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	item = &(latencys->ocf_latency_items[STATS_TYPE_INVALID]);
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Invalid max latency |", (*item).max, "| Microsecond |");
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| OCF Invalid min latency |", (*item).min, "| Microsecond |");
	STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, "| OCF Invalid avg latency |", (*item).avg, "| Microsecond |");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	item = &(latencys->backend_latency_items[STATS_TYPE_READ]);
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| Cache Read max latency  |", (*item).max, "| Microsecond |");
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| Cache Read min latency  |", (*item).min, "| Microsecond |");
	STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, "| Cache Read avg latency  |", (*item).avg, "| Microsecond |");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n");
	item = &(latencys->backend_latency_items[STATS_TYPE_WRITE]);
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| Cache Write max latency |", (*item).max, "| Microsecond |");
	STATS_DUMP_FIELD_WITHOUT_F(buf, "| Cache Write min latency |", (*item).min, "| Microsecond |");
	STATS_DUMP_DOUBLE_FIELD_WITHOUT_F(buf, "| Cache Write avg latency |", (*item).avg, "| Microsecond |");
	strbuf_write_str(buf, "+-------------------------+----------------------+---+-------------+\n\n");
}

static void _stats_dump_inout(struct strbuf *buf, struct ocf_stats_ocf_inout_reqs *inout)
{
	strbuf_write_str(buf, "+-------------------+----------------------+--------+----------+\n");
	strbuf_write_str(buf, "| Access statistics |         Count        |    %   |   Units  |\n");
	strbuf_write_str(buf, "+-------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| OCF Read Enter    |", inout->in_reqs[STATS_TYPE_READ], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Write Enter   |", inout->in_reqs[STATS_TYPE_WRITE], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Lookup Enter  |", inout->in_reqs[STATS_TYPE_LOOKUP], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Invalid Enter |", inout->in_reqs[STATS_TYPE_INVALID], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Total Enter   |", inout->total_in, "| Requests |");
	strbuf_write_str(buf, "+-------------------+----------------------+--------+----------+\n");
	STATS_DUMP_FIELD(buf, "| OCF Read Leave    |", inout->out_reqs[STATS_TYPE_READ], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Write Leave   |", inout->out_reqs[STATS_TYPE_WRITE], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Lookup Leave  |", inout->out_reqs[STATS_TYPE_LOOKUP], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Invalid Leave |", inout->out_reqs[STATS_TYPE_INVALID], "| Requests |");
	STATS_DUMP_FIELD(buf, "| OCF Total Leave   |", inout->total_out, "| Requests |");
	strbuf_write_str(buf, "+-------------------+----------------------+--------+----------+\n\n");
}

static void _stats_dump_cache_cb(ocf_cache_t cache, void *priv, int error)
{
	struct stats_dump_ctx *dump_ctx = priv;
	struct strbuf *buf = dump_ctx->buf;
	struct ocf_stats_usage *usage = env_malloc(sizeof(*usage), ENV_MEM_NORMAL);
	if (!usage)
		goto err_usage;
	struct ocf_stats_requests *req = env_malloc(sizeof(*req), ENV_MEM_NORMAL);
	if (!req)
		goto err_req;
	struct ocf_stats_blocks *blocks = env_malloc(sizeof(*blocks), ENV_MEM_NORMAL);
	if (!blocks)
		goto err_blocks;
	struct ocf_stats_errors *errors = env_malloc(sizeof(*errors), ENV_MEM_NORMAL);
	if (!errors)
		goto err_errors;
	struct ocf_stats_success *success  = env_malloc(sizeof(*success), ENV_MEM_NORMAL);
	if (!success)
		goto err_success;
	struct ocf_stats_latencys *latencys = env_malloc(sizeof(*latencys), ENV_MEM_NORMAL);
	if (!latencys)
		goto err_latencys;
	struct ocf_stats_ocf_inout_reqs *inout = env_malloc(sizeof(*inout), ENV_MEM_NORMAL);
	if (!inout)
		goto err_inout;

	ocf_stats_collect_cache(cache, usage, req, blocks, errors, success, latencys, inout);

	/* format usage stats */
	_stats_dump_usage(buf, usage);
	/* format req stats */
	_stats_dump_req(buf, req);
	/* format blocks stats */
	_stats_dump_blocks(buf, blocks);
	/* format error stats */
	_stats_dump_errors(buf, errors);
	/* format success stats */
	_stats_dump_success(buf, success);
	/* format latency stats */
	_stats_dump_latencys(buf, latencys);
	/* format ocf in out reqs stats */
	_stats_dump_inout(buf, inout);

	env_free(inout);
err_inout:
	env_free(latencys);
err_latencys:
	env_free(success);
err_success:
	env_free(errors);
err_errors:
	env_free(blocks);
err_blocks:
	env_free(req);
err_req:
	env_free(usage);
err_usage:
	/* tell the caller that we have done */
	env_completion_complete(dump_ctx->cmpl);
}

struct strbuf* ocf_stats_dump_cache(ocf_ctx_t ctx, const char *cache_name)
{
	struct stats_dump_ctx dump_ctx;
	struct strbuf *b;
	env_completion cmpl;
	ocf_cache_t cache;

	if(!cache_name) {
		return NULL;
	}

	b = new_strbuf();
	if(b == NULL) {
		ocf_log(ctx, log_err, "Failed to allcation memory for string buffer\n");
		return b;
	}

	if(ocf_mngt_cache_get_by_name(ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		/* no cache found */
		strbuf_write_format_str(b, "cache named %.*s not exist\n",
					OCF_CACHE_NAME_SIZE, cache_name);
		goto end;
	}

	env_completion_init(&cmpl);
	dump_ctx.buf = b;
	dump_ctx.cmpl = &cmpl;
	ocf_mngt_cache_read_lock(cache, _stats_dump_cache_cb, &dump_ctx);
	env_completion_wait(&cmpl);
	env_completion_destroy(&cmpl);

	ocf_mngt_cache_put(cache);

end:
	strbuf_write_end(b);

	return b;
}

static int _reset_stats(ocf_core_t core, void *cntx)
{
	ocf_core_stats_reset(core);
	return 0;
}

int ocf_stats_reset_cache(ocf_ctx_t ctx, const char *cache_name)
{
	ocf_cache_t cache;

	if(ocf_mngt_cache_get_by_name(ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		return -OCF_ERR_CACHE_NOT_EXIST;
	}

	if (ocf_cache_is_standby(cache))
		return -OCF_ERR_CACHE_STANDBY;

	return ocf_core_visit(cache, _reset_stats, NULL, true);
}

static int _reset_lattency_stats(ocf_core_t core, void *cntx)
{
	ocf_core_stats_reset_lattency(core);
	return 0;
}

int ocf_stats_reset_lattency(ocf_ctx_t ctx, const char *cache_name)
{
	ocf_cache_t cache;

	if(ocf_mngt_cache_get_by_name(ctx, cache_name, OCF_CACHE_NAME_SIZE, &cache)) {
		return -OCF_ERR_CACHE_NOT_EXIST;
	}

	if (ocf_cache_is_standby(cache))
		return -OCF_ERR_CACHE_STANDBY;

	return ocf_core_visit(cache, _reset_lattency_stats, NULL, true);
}
