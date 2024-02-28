/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ocf/ocf.h>
#include <vector>
#include <functional>
#include "device.h"
#include "volume.h"
#include "ctx.h"
#include "log.h"

using namespace std;

struct lava_volume {
	const char *name;
	std::vector<uint64_t> chunk_ids;
};

struct no_io_volume {
	uint64_t cache_line_size;
};

/*
 * In open() function we store uuid data as volume name (for debug messages)
 * and allocate chunk to excute IO operation.
 */
static int lava_volume_open(ocf_volume_t volume, void *volume_params)
{
	int ret;
	const struct ocf_volume_uuid *uuid = ocf_volume_get_uuid(volume);
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);
	struct lava_volume_param *param = (struct lava_volume_param*)volume_params;

	lava_volume->name = ocf_uuid_to_str(uuid);
	ret = AllocChunks(param->chunk_num, &lava_volume->chunk_ids);

	if (ret) {
		ocf_adaptor_log(OCF_LOG_ERROR, "Lava chunk open faied with ret:%d\n", ret);
		return -1;
	}

	ocf_adaptor_log(OCF_LOG_INFO, "VOL OPEN: (name: %s)\n", lava_volume->name);

	return 0;
}

/*
 * In close() function we just free chunk allocated in open().
 */
static void lava_volume_close(ocf_volume_t volume)
{
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);

	FreeChunks(&lava_volume->chunk_ids);

	ocf_adaptor_log(OCF_LOG_INFO, "VOL CLOSE: (name: %s)\n", lava_volume->name);
}

static void lava_volume_submit_io_cb(int ret, void *context)
{
	Request *req = (Request*)context;
	struct ocf_io *io = (struct ocf_io*)req->user_ctx;
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);

	if (ret) {
		lava_volume_io->ret = ret;
	}

	delete req;
	if (env_atomic_dec_return(&lava_volume_io->req_cnt) == 0) {
		io->end(io, ret);
 	}
}

/*
 * In submit_io() function use chunk API to finish IO operation.
 */
static void lava_volume_submit_io(struct ocf_io *io)
{
	int ret;
	uint64_t addr = io->addr;
	uint32_t io_length = io->bytes;
	uint32_t submitted_len = 0;
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);
	struct volume_data *data;
	struct lava_volume *lava_volume;

	data = (struct volume_data*)ocf_io_get_data(io);
	lava_volume = (struct lava_volume*)ocf_volume_get_priv(ocf_io_get_volume(io));

	env_atomic_set(&lava_volume_io->req_cnt, 1);
	while (io_length > 0) {
		Segment s;
		Request *req = new Request();
		uint32_t chunk_remain = LAVA_CHUNK_SIZE - ((addr + submitted_len) % LAVA_CHUNK_SIZE);
		s.offset = (addr + submitted_len) % LAVA_CHUNK_SIZE;
		if (chunk_remain > io_length) {
			s.length = io_length;
			io_length = 0;
		} else {
			s.length = chunk_remain;
			io_length -= chunk_remain;
		}
		s.data = data->ptr + lava_volume_io->offset + submitted_len;
		req->chunk_id = lava_volume->chunk_ids[((addr + submitted_len) / LAVA_CHUNK_SIZE)];
		req->segments.push_back(s);
		req->user_ctx = io;
		req->cb = lava_volume_submit_io_cb;
		submitted_len += s.length;
		env_atomic_inc(&lava_volume_io->req_cnt);

		if (io->dir == OCF_WRITE) {
			ret = AioWrite(req);
		} else {
			ret = AioRead(req);
		}

		if (ret) {
			ocf_adaptor_log(OCF_LOG_ERROR, "Chunk IO failed with ret:%d", ret);
			lava_volume_submit_io_cb(ret, req);
		}
	}

	if (env_atomic_dec_return(&lava_volume_io->req_cnt) == 0) {
		io->end(io, ret);
	}

	ocf_adaptor_log(OCF_LOG_DEBUG, "VOL: (name: %s), IO: (dir: %s, addr: %ld, bytes: %d)\n",
			lava_volume->name, io->dir == OCF_READ ? "read" : "write",
			io->addr, io->bytes);
}

/*
 * We don't need to implement submit_flush(). Just complete io with success.
 */
static void lava_volume_submit_flush(struct ocf_io *io)
{
	io->end(io, 0);
}

/*
 * We don't need to implement submit_discard(). Just complete io with success.
 */
static void lava_volume_submit_discard(struct ocf_io *io)
{
	io->end(io, 0);
}

/*
 * Let's set maximum io size to 128 KiB.
 */
static unsigned int lava_volume_get_max_io_size(ocf_volume_t volume)
{
	return 128 * 1024;
}

/*
 * Return volume size.
 */
static uint64_t lava_volume_get_length(ocf_volume_t volume)
{
	struct lava_volume *lava_volume = (struct lava_volume*)ocf_volume_get_priv(volume);

	return lava_volume->chunk_ids.size() * LAVA_CHUNK_SIZE;
}

/*
 * In set_data() we just assing data and offset to io.
 */
static int lava_volume_io_set_data(struct ocf_io *io, ctx_data_t *data,
		uint32_t offset)
{
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);

	lava_volume_io->data = (struct volume_data*)data;
	lava_volume_io->offset = offset;

	return 0;
}

/*
 * In get_data() return data stored in io.
 */
static ctx_data_t *lava_volume_io_get_data(struct ocf_io *io)
{
	struct lava_volume_io *lava_volume_io = (struct lava_volume_io*)ocf_io_get_priv(io);

	return lava_volume_io->data;
}

static int no_io_volume_open(ocf_volume_t volume, void *volume_params)
{
    struct no_io_volume *v = (struct no_io_volume*)ocf_volume_get_priv(volume);
    v->cache_line_size = *(uint64_t *)volume_params;
    return 0;
}

static void no_io_volume_close(ocf_volume_t volume)
{
}

static void no_io_volume_submit_io(struct ocf_io *io)
{
}

static void no_io_volume_submit_flush(struct ocf_io *io)
{
}

static void no_io_volume_submit_discard(struct ocf_io *io)
{
}

static unsigned int no_io_volume_get_max_io_size(ocf_volume_t volume)
{
    return 128 * 1024;
}

static uint64_t no_io_volume_get_length(ocf_volume_t volume)
{
    struct no_io_volume *v = (struct no_io_volume*)ocf_volume_get_priv(volume);
    return (1UL << 42) * v->cache_line_size;
}

static int no_io_volume_io_set_data(struct ocf_io *io, ctx_data_t *data,
        uint32_t offset)
{
    return 0;
}


static ctx_data_t *no_io_volume_io_get_data(struct ocf_io *io)
{
    return NULL;
}

/*
 * This structure contains volume properties. It describes volume
 * type, which can be later instantiated as backend storage for cache
 * or core.
 */
static struct ocf_volume_properties volume_properties;

static struct ocf_volume_properties no_io_volume_properties;

/*
 * This function registers volume type in OCF context.
 * It should be called just after context initialization.
 */
int volume_init(ocf_ctx_t ocf_ctx)
{
	volume_properties.name = "Chunk volume",
	volume_properties.io_priv_size = sizeof(struct lava_volume_io),
	volume_properties.volume_priv_size = sizeof(struct lava_volume),
	volume_properties.caps.atomic_writes = 0;

	volume_properties.ops.open = lava_volume_open;
	volume_properties.ops.close = lava_volume_close;
	volume_properties.ops.submit_io = lava_volume_submit_io;
	volume_properties.ops.submit_flush = lava_volume_submit_flush;
	volume_properties.ops.submit_discard = lava_volume_submit_discard;
	volume_properties.ops.get_max_io_size = lava_volume_get_max_io_size;
	volume_properties.ops.get_length = lava_volume_get_length;

	volume_properties.io_ops.set_data = lava_volume_io_set_data;
	volume_properties.io_ops.get_data = lava_volume_io_get_data;

	no_io_volume_properties.name = "no io volume",
	no_io_volume_properties.io_priv_size = 0,
	no_io_volume_properties.volume_priv_size = sizeof(struct no_io_volume),
	no_io_volume_properties.caps.atomic_writes = 0;

	no_io_volume_properties.ops.open = no_io_volume_open;
	no_io_volume_properties.ops.close = no_io_volume_close;
	no_io_volume_properties.ops.submit_io = no_io_volume_submit_io;
	no_io_volume_properties.ops.submit_flush = no_io_volume_submit_flush;
	no_io_volume_properties.ops.submit_discard = no_io_volume_submit_discard;
	no_io_volume_properties.ops.get_max_io_size = no_io_volume_get_max_io_size;
	no_io_volume_properties.ops.get_length = no_io_volume_get_length;

	no_io_volume_properties.io_ops.set_data = no_io_volume_io_set_data;
	no_io_volume_properties.io_ops.get_data = no_io_volume_io_get_data;

	int ret = ocf_ctx_register_volume_type(ocf_ctx, LAVA_VOL_TYPE, &volume_properties);
	if (ret) {
		return ret;
	}

	ret = ocf_ctx_register_volume_type(ocf_ctx, CORE_VOL_TYPE, &no_io_volume_properties);

	return ret;
}

/*
 * This function unregisters volume type in OCF context.
 * It should be called just before context cleanup.
 */
void volume_cleanup(ocf_ctx_t ocf_ctx)
{
	ocf_ctx_unregister_volume_type(ocf_ctx, LAVA_VOL_TYPE);
}