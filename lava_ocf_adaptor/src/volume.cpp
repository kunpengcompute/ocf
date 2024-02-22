/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ocf/ocf.h>
#include <vector>
#include <functional>
#include "volume.h"
#include "ctx.h"
#include "log.h"

using namespace std;

struct segment {
	uint32_t offset;
	uint32_t length;
	char *data;
};

struct Request {
	uint64_t chunk_id;
	std::vector<segment> segments;
	void *user_ctx;
	std::function<void(int ret, void *context)> cb;
};

struct lava_volume {
	const char *name;
	std::vector<uint64_t> chunk_ids;
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

	if (--lava_volume_io->req_cnt == 0) {
		io->end(io, ret);
		free(req);
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

	do {
		segment s;
		Request *req = (Request*)malloc(sizeof(struct Request));
		uint32_t chunk_remain = LAVA_CHUNK_SIZE - (addr % LAVA_CHUNK_SIZE);
		s.offset = addr + submitted_len;
		if (chunk_remain > io_length) {
			s.length = io_length;
			io_length = 0;
		} else {
			s.length = chunk_remain;
			io_length -= chunk_remain;
		}
		s.data = data->ptr + submitted_len;
		req->chunk_id = lava_volume->chunk_ids[(addr / LAVA_CHUNK_SIZE)];
		req->segments.push_back(s);
		req->user_ctx = io;
		req->cb = lava_volume_submit_io_cb;
		submitted_len += s.length;
		lava_volume_io->req_cnt++;

		if (io->dir = OCF_WRITE) {
			ret = AioWrite(req);
		} else {
			ret = AioRead(req);
		}

		if (ret) {
			ocf_adaptor_log(OCF_LOG_ERROR, "Chunk IO failed with ret:%d", ret);
			lava_volume_submit_io_cb(ret, req);
		}

		addr += s.length;
	} while(io_length > 0);

	ocf_adaptor_log(OCF_LOG_INFO, "VOL: (name: %s), IO: (dir: %s, addr: %ld, bytes: %d)\n",
			lava_volume->name, io->dir == OCF_READ ? "read" : "write",
			io->addr, io->bytes);
	
	return 0;
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

/*
 * This structure contains volume properties. It describes volume
 * type, which can be later instantiated as backend storage for cache
 * or core.
 */
const struct ocf_volume_properties volume_properties = {
	.name = "Chunk volume",
	.io_priv_size = sizeof(struct lava_volume_io),
	.volume_priv_size = sizeof(struct lava_volume),
	.caps = {
		.atomic_writes = 0,
	},
	.ops = {
		.open = lava_volume_open,
		.close = lava_volume_close,
		.submit_io = lava_volume_submit_io,
		.submit_flush = lava_volume_submit_flush,
		.submit_discard = lava_volume_submit_discard,
		.get_max_io_size = lava_volume_get_max_io_size,
		.get_length = lava_volume_get_length,
	},
	.io_ops = {
		.set_data = lava_volume_io_set_data,
		.get_data = lava_volume_io_get_data,
	},
};

/*
 * This function registers volume type in OCF context.
 * It should be called just after context initialization.
 */
int volume_init(ocf_ctx_t ocf_ctx)
{
	return ocf_ctx_register_volume_type(ocf_ctx, LAVA_VOL_TYPE,
			&volume_properties);
}

/*
 * This function unregisters volume type in OCF context.
 * It should be called just before context cleanup.
 */
void volume_cleanup(ocf_ctx_t ocf_ctx)
{
	ocf_ctx_unregister_volume_type(ocf_ctx, LAVA_VOL_TYPE);
}