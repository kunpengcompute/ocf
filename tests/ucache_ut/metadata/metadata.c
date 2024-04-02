/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ut for engine_uc main file
 * Author: hebo
 * Create: 2024-02-19
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ocf/ocf.h>
#include "ut_ctx.h"
#include "ocf_adaptor.h"
#include "volume.h"
#include "ocf_env.h"
#include "ocf/ocf_def.h"

/*
* add when submmit io to OCF and subtract from io's callback.
* So we can judge the completion status of all ios at the end.
*/
static env_atomic poll_num;

struct req_identifier {
	/* read or write buffer*/
	struct volume_data *data;
	/* print info in callback */
	char *info;
};

#define REGION_SIZE (32 * GiB)
#define IO_DELAY 200

static struct req_context *create_request(
	int slot_id, uint64_t offset, uint64_t len, struct req_identifier *identify)
{
	struct req_context *req;
	req = (struct req_context*)malloc(sizeof(*req));
	assert_non_null(req);
	memset(req, 0, sizeof(*req));
	req->req_identifier = identify;
	req->io_worker_id = slot_id;
	req->slot_id = slot_id;
	req->region_id = offset / REGION_SIZE;
	req->offset = offset % REGION_SIZE;
	req->len = len;
	req->buffer = identify->data->ptr;

	return req;
}

static int generic_callback(int32_t ret, struct req_context *ctx)
{
	struct req_identifier *identify = ctx->req_identifier;
	// assert_int_equal(ret, 0);
	env_atomic_dec(&poll_num);
	
	/* free data */
	ut_ctx_data_free(identify->data);
	free(ctx);
	return ret;
}

static int read_callback(int32_t ret, struct req_context *ctx)
{
	struct req_identifier *identify = ctx->req_identifier;
	// assert_int_equal(ret, 0);
	env_atomic_dec(&poll_num);
	
	/* free data */
	ut_ctx_data_free(identify->data);
	free(ctx);
	return ret;
}

static int lookup_miss_callback(int32_t ret, struct req_context *ctx)
{
	struct req_identifier *identify = ctx->req_identifier;
	/* -2 for cache miss */
	// assert_int_equal(ret, -2);
	env_atomic_dec(&poll_num);
	
	/* free data */
	ut_ctx_data_free(identify->data);
	free(ctx);
	return ret;
}

static void ocf_show_cache_stats()
{
	struct ocf_dump_info *info = NULL;
	info = ocf_dump_cache_stats();
	assert_non_null(info);
	printf("stats info\n%s", info->buf);
	ocf_release_dump_info(info);
}

static void process_bar(uint64_t now, uint64_t total, const char *msg)
{
	uint64_t process_bar = 10 * now / total;
	if (now % (total / 10) == 0) {
		print_message("%d0 %% of %s IO are processed\n", process_bar, msg);
	}
}

static void create_big_cache_8k_48TB_test_IO(void **state)
{
	int ret;
	struct req_context *req = NULL;
	struct req_identifier identify;
	struct ocf_config cfg = {
		/* 8k cacheline*/
		.cache_line_size = 8 * KiB,
		.io_worker_num = 6,
		.core_num = 6,
		.core_mask = 0X3F,
		/* 256 TiB cache */
		.cache_capacity = 48 * TiB,
	};

	/* init test */
	ret = ocf_init(&cfg);
	assert_int_equal(ret, 0);

	/* add core (1 slot)*/
	ret = ocf_add_core(0);
	assert_int_equal(ret, 0);

	// 使IO数据量大于cache容量
	uint64_t total_bytes = cfg.cache_capacity;
	// uint64_t total_bytes = cfg.cache_capacity * 1.1;
	uint64_t io_length = 1 * MiB;
	uint64_t total_ios = total_bytes / io_length;

	/* write 8k sequential-read */
	for (uint64_t i = 0; i < total_ios; i++) {
		uint64_t offset = i * io_length;
		struct volume_data *data = ut_ctx_data_alloc(io_length / PAGE_SIZE);
		ut_ctx_data_zero(data, io_length);
		identify.data = data;
		req = create_request(0, offset, io_length, &identify);
		req->cb = generic_callback;
		env_atomic_inc(&poll_num);
		ret = ocf_put(req);
		assert_int_equal(ret, 0);
		usleep(IO_DELAY);
		ocf_poll(0, 1);
		process_bar(offset + io_length, total_bytes, "write");
	}

	/* read 8k sequential-read, we will get hit*/
	for (uint64_t i = 0; i < total_ios; i++) {
		uint64_t offset = i * io_length;
		struct volume_data *data = ut_ctx_data_alloc(io_length / PAGE_SIZE);
		ut_ctx_data_zero(data, io_length);
		identify.data = data;
		req = create_request(0, offset, io_length, &identify);
		req->cb = read_callback;
		env_atomic_inc(&poll_num);
		ret = ocf_lookup(req);
		assert_int_equal(ret, 0);
		usleep(IO_DELAY);
		ocf_poll(0, 1);
		process_bar(offset + io_length, total_bytes, "lookup");
	}

	/* dump stats test */
	if (env_atomic_read(&poll_num) > 0) {
		print_message("Waiting %d IO to be processed\n", env_atomic_read(&poll_num));
		env_msleep(1000);
	}
	// assert_int_equal(env_atomic_read(&poll_num), 0);
	ocf_show_cache_stats();

	/* invalid */
	for (uint64_t i = 0; i < total_ios; i++) {
		uint64_t offset = i * io_length;
		struct volume_data *data = ut_ctx_data_alloc(io_length / PAGE_SIZE);
		ut_ctx_data_zero(data, io_length);
		identify.data = data;
		req = create_request(0, offset, io_length, &identify);
		req->cb = generic_callback;
		env_atomic_inc(&poll_num);
		ret = ocf_invalid(req);
		assert_int_equal(ret, 0);
		usleep(IO_DELAY);
		ocf_poll(0, 1);
		process_bar(offset + io_length, total_bytes, "invalid");
	}

	/* lookup again, we will get miss */
	for (uint64_t i = 0; i < total_ios; i++) {
		uint64_t offset = i * io_length;
		struct volume_data *data = ut_ctx_data_alloc(io_length / PAGE_SIZE);
		ut_ctx_data_zero(data, io_length);
		identify.data = data;
		req = create_request(0, offset, io_length, &identify);
		req->cb = lookup_miss_callback;
		env_atomic_inc(&poll_num);
		ret = ocf_lookup(req);
		assert_int_equal(ret, 0);
		usleep(IO_DELAY);
		ocf_poll(0, 1);
		process_bar(offset + io_length, total_bytes, "lookup");
	}
	
	/* dump stats test */
	if (env_atomic_read(&poll_num) != 0) {
		print_message("Waiting %d IO to be processed\n", env_atomic_read(&poll_num));
		env_msleep(1000);
	}
	// assert_int_equal(env_atomic_read(&poll_num), 0);
	ocf_show_cache_stats();
	
	/* exit test */
	ocf_exit();

	print_message("We successfully created 48TiB cache "
		"and test IO stream\n");
}

static void create_big_cache_64k_256TB_test(void **state)
{
	int ret;
	struct ocf_config cfg = {
		/* 64k cacheline*/
		.cache_line_size = 64 * KiB,
		.io_worker_num = 1,
		.core_num = 6,
		.core_mask = 0X3F,
		/* 256 TiB cache */
		.cache_capacity = 256 * TiB,
	};

	/* init test */
	ret = ocf_init(&cfg);
	assert_int_equal(ret, 0);

	/* add core (1 slot)*/
	ret = ocf_add_core(0);
	assert_int_equal(ret, 0);

	ocf_show_cache_stats();

	/* exit test */
	ocf_exit();

	print_message("We successfully created 256TiB cache "
			"in 64KiB cacheline and free it\n");
}

static void create_big_cache_8k_48TB_test(void **state)
{
	int ret;
	struct ocf_config cfg = {
		/* 8k cacheline*/
		.cache_line_size = 8 * KiB,
		.io_worker_num = 1,
		.core_num = 6,
		.core_mask = 0X3F,
		/* 48 TiB cache */
		.cache_capacity = 48 * TiB,
	};

	/* init test */
	ret = ocf_init(&cfg);
	assert_int_equal(ret, 0);

	/* add core (1 slot)*/
	ret = ocf_add_core(0);
	assert_int_equal(ret, 0);

	ocf_show_cache_stats();

	/* exit test */
	ocf_exit();

	print_message("We successfully created 48TiB cache "
			"in 8KiB cacheline and free it\n");
}



int main(void)
{
    const struct CMUnitTest tests[] = {
		cmocka_unit_test(create_big_cache_64k_256TB_test),
		cmocka_unit_test(create_big_cache_8k_48TB_test),
		cmocka_unit_test(create_big_cache_8k_48TB_test_IO),
    };

    print_message("Unit test for ocf_metadata\n");

    return cmocka_run_group_tests(tests, NULL, NULL);
}
