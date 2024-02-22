/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <ocf/ocf.h>
#include <stdio.h>
#include "log.h"
#include "volume.h"
#include "ctx.h"

#define MAX_BUF 1024
#define PAGE_SIZE 4096

/*
 * Allocate structure representing data for io operations.
 */
ctx_data_t *lava_ctx_data_alloc(uint32_t pages)
{
	struct volume_data *data;

	data = malloc(sizeof(*data));
	data->ptr = malloc(pages * PAGE_SIZE);
	data->offset = 0;

	return data;
}

/*
 * Free data structure.
 */
void lava_ctx_data_free(ctx_data_t *ctx_data)
{
	struct volume_data *data = ctx_data;

	if (!data)
		return;

	free(data->ptr);
	free(data);
}

/*
 * This function is supposed to set protection of data pages against swapping.
 * Can be non-implemented if not needed.
 */
static int lava_ctx_data_mlock(ctx_data_t *ctx_data)
{
	return 0;
}

/*
 * Stop protecting data pages against swapping.
 */
static void lava_ctx_data_munlock(ctx_data_t *ctx_data)
{
}

/*
 * Read data into flat memory buffer.
 */
static uint32_t lava_ctx_data_read(void *dst, ctx_data_t *src, uint32_t size)
{
	struct volume_data *data = src;

	memcpy(dst, data->ptr + data->offset, size);
	data->offset += size;

	return size;
}

/*
 * Write data from flat memory buffer.
 */
static uint32_t lava_ctx_data_write(ctx_data_t *dst, const void *src, uint32_t size)
{
	struct volume_data *data = dst;

	memcpy(data->ptr + data->offset, src, size);
	data->offset += size;

	return size;
}

/*
 * Fill data with zeros.
 */
static uint32_t lava_ctx_data_zero(ctx_data_t *dst, uint32_t size)
{
	struct volume_data *data = dst;

	memset(data->ptr + data->offset, 0, size);
	data->offset += size;

	return size;
}

/*
 * Perform seek operation on data.
 */
static uint32_t lava_ctx_data_seek(ctx_data_t *dst, ctx_data_seek_t seek,
		uint32_t offset)
{
	struct volume_data *data = dst;

	switch (seek) {
	case ctx_data_seek_begin:
		data->offset = offset;
		break;
	case ctx_data_seek_current:
		data->offset += offset;
		break;
	}

	return offset;
}

/*
 * Copy data from one structure to another.
 */
static uint64_t lava_ctx_data_copy(ctx_data_t *dst, ctx_data_t *src,
		uint64_t to, uint64_t from, uint64_t bytes)
{
	struct volume_data *data_dst = dst;
	struct volume_data *data_src = src;

	memcpy(data_dst->ptr + to, data_src->ptr + from, bytes);

	return bytes;
}

/*
 * Perform secure erase of data (e.g. fill pages with zeros).
 * Can be left non-implemented if not needed.
 */
static void lava_ctx_data_secure_erase(ctx_data_t *ctx_data)
{
}

/*
 * Initialize cleaner thread. Cleaner thread is left non-implemented,
 * to keep this example as simple as possible.
 */
static int lava_ctx_cleaner_init(ocf_cleaner_t c)
{
	return 0;
}

/*
 * Kick cleaner thread. Cleaner thread is left non-implemented,
 * to keep this example as simple as possible.
 */
static void lava_ctx_cleaner_kick(ocf_cleaner_t c)
{
}

/*
 * Stop cleaner thread. Cleaner thread is left non-implemented, to keep
 * this example as simple as possible.
 */
static void lava_ctx_cleaner_stop(ocf_cleaner_t c)
{
}


static int lava_ctx_logger_print(ocf_logger_t logger, ocf_logger_lvl_t lvl, const char *fmt, va_list args)
{
	int ocf_log_lvl = lvl;
	char buf[MAX_BUF];
	log_print_func print = get_log_print();

	int ret = vsnprintf(buf, sizeof(buf), fmt, args);
	if (ret < 0) {
		print(OCF_LOG_ERROR, "the print is too long\n");
		return -1;
	}

	return print((ocf_log_level)ocf_log_lvl, buf);
}

static const struct ocf_ctx_config ctx_cfg = {
	.name = "OCF LAVA",
	.ops = {
		.data = {
			.alloc = lava_ctx_data_alloc,
			.free = lava_ctx_data_free,
			.mlock = lava_ctx_data_mlock,
			.munlock = lava_ctx_data_munlock,
			.read = lava_ctx_data_read,
			.write = lava_ctx_data_write,
			.zero = lava_ctx_data_zero,
			.seek = lava_ctx_data_seek,
			.copy = lava_ctx_data_copy,
			.secure_erase = lava_ctx_data_secure_erase,
		},

		.cleaner = {
			.init = lava_ctx_cleaner_init,
			.kick = lava_ctx_cleaner_kick,
			.stop = lava_ctx_cleaner_stop,
		},

		.logger = {
			.print = lava_ctx_logger_print,
		},
	},
};

int ctx_init(ocf_ctx_t *ctx)
{
	int32_t ret = ocf_ctx_create(ctx, &ctx_cfg);
	if (ret) {
		return ret;
	}

	ret = volume_init(*ctx);
	if (ret) {
		ocf_ctx_put(*ctx);
		return ret;
	}
	return 0;
}

void ctx_cleanup(ocf_ctx_t ctx)
{
	volume_cleanup(ctx);
	ocf_ctx_put(ctx);
}