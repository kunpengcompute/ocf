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

static struct req_context *create_request(
	int slot_id, int region_id, uint64_t offset, uint64_t len, struct req_identifier *identify)
{
	struct req_context *req;
	req = (struct req_context*)malloc(sizeof(*req));
	assert_non_null(req);
	memset(req, 0, sizeof(*req));
	req->req_identifier = identify;
	req->io_worker_id = slot_id;
	req->slot_id = slot_id;
	req->region_id = region_id;
	req->offset = offset;
	req->len = len;
	req->buffer = identify->data->ptr;

	return req;
}

static int generic_callback(int32_t ret, struct req_context *ctx)
{
	struct req_identifier *identify = ctx->req_identifier;
	printf("req identifier [%s] done with %d\n", identify->info, ret);
	assert_int_equal(ret, 0);
	env_atomic_dec(&poll_num);
	
	/* free data */
	ut_ctx_data_free(identify->data);
	free(ctx);
	return ret;
}

static int read_callback(int32_t ret, struct req_context *ctx)
{
	struct req_identifier *identify = ctx->req_identifier;
	printf("req identifier [%s] done with %d, read data[%s]\n", identify->info, ret, ctx->buffer);
	assert_int_equal(ret, 0);
	env_atomic_dec(&poll_num);
	
	/* free data */
	ut_ctx_data_free(identify->data);
	free(ctx);
	return ret;
}

static int lookup_miss_callback(int32_t ret, struct req_context *ctx)
{
	struct req_identifier *identify = ctx->req_identifier;
	printf("req identifier [%s] done with %d\n", identify->info, ret);
	/* -2 for cache miss */
	assert_int_equal(ret, -2);
	env_atomic_dec(&poll_num);
	
	/* free data */
	ut_ctx_data_free(identify->data);
	free(ctx);
	return ret;
}

static int align_failed_callback(int32_t ret, struct req_context *ctx)
{
	assert_int_equal(ret, -2);
}

static void engine_uc_test(void **state)
{
	int ret;
	struct req_context *req = NULL;
	struct ocf_dump_info *info = NULL;
	struct req_identifier identify;
	struct ocf_config cfg = {
		/* 8k cacheline*/
		.cache_line_size = 8192,
		.io_worker_num = 24,
		.core_num = 6,
		.core_mask = 0X3F,
		/* 1G cache */
		.cache_capacity = 1 * (1ULL << 30),
	};

	/* init test */
	ret = ocf_init(&cfg);
	assert_int_equal(ret, 0);

	/* add core (1 slot)*/
	ret = ocf_add_core(0);
	assert_int_equal(ret, 0);

	/* dump info test */
	info = ocf_dump_cache_core_info();
	assert_non_null(info);
	printf("ocf info\n%s", info->buf);
	ocf_release_dump_info(info);

	/* write something */
	struct volume_data *data = ut_ctx_data_alloc(1);
	ut_ctx_data_zero(data, 1 * PAGE_SIZE);
	strcpy(data->ptr, "data for unit test");
	identify.data = data;
	identify.info = "write";
	req = create_request(0, 0, 0, 1 * PAGE_SIZE, &identify);
	req->cb = generic_callback;
	env_atomic_inc(&poll_num);
	ret = ocf_put(req);
	assert_int_equal(ret, 0);
	/* sleep because of backend latency */
	usleep(1000);
	ocf_poll(0, 3);

	/* read */
	data = ut_ctx_data_alloc(1);
	ut_ctx_data_zero(data, 1 * PAGE_SIZE);
	identify.data = data;
	identify.info = "read";
	req = create_request(0, 0, 0, 1 * PAGE_SIZE, &identify);
	req->cb = read_callback;
	env_atomic_inc(&poll_num);
	ret = ocf_get(req);
	assert_int_equal(ret, 0);
	/* sleep because of backend latency */
	usleep(1000);
	ocf_poll(0, 3);

	/* lookup */
	data = ut_ctx_data_alloc(1);
	ut_ctx_data_zero(data, 1 * PAGE_SIZE);
	identify.data = data;
	identify.info = "lookup exist";
	req = create_request(0, 0, 0, 1 * PAGE_SIZE, &identify);
	req->cb = generic_callback;
	env_atomic_inc(&poll_num);
	ret = ocf_lookup(req);
	assert_int_equal(ret, 0);
	/* sleep because of backend latency */
	usleep(1000);
	ocf_poll(0, 3);

	/* invalid */
	data = ut_ctx_data_alloc(1);
	ut_ctx_data_zero(data, 1 * PAGE_SIZE);
	identify.data = data;
	identify.info = "invalid";
	req = create_request(0, 0, 0, 1 * PAGE_SIZE, &identify);
	req->cb = generic_callback;
	env_atomic_inc(&poll_num);
	ret = ocf_range_invalid(req);
	assert_int_equal(ret, 0);
	/* sleep because of backend latency */
	usleep(1000);
	ocf_poll(0, 3);

	/* lookup again, we will get miss */
	data = ut_ctx_data_alloc(1);
	ut_ctx_data_zero(data, 1 * PAGE_SIZE);
	identify.data = data;
	identify.info = "lookup miss";
	req = create_request(0, 0, 0, 1 * PAGE_SIZE, &identify);
	req->cb = lookup_miss_callback;
	env_atomic_inc(&poll_num);
	ret = ocf_lookup(req);
	assert_int_equal(ret, 0);
	/* sleep because of backend latency */
	usleep(1000);
	ocf_poll(0, 3);

	/* sleep 3s for all ios done*/
	int times = 3000;
	while (env_atomic_read(&poll_num) != 0 && times-- > 0) {
		usleep(1000);
	}
	
	/* dump stats test */
	info = ocf_dump_cache_stats();
	assert_non_null(info);
	printf("stats info\n%s", info->buf);
	ocf_release_dump_info(info);

	/* ensure that all ios have been processed */
	assert_int_equal(env_atomic_read(&poll_num), 0);
}

static void ocf_exit_test(void **state)
{
	int ret;
	/* remove success */
	ret = ocf_remove_core(0);
	assert_int_equal(ret, 0);

	/* remove failed */
	ret = ocf_remove_core(5);
	assert_int_equal(ret, -1001);
	ocf_exit();
}

static void ocf_submit_failed_test(void **state)
{
	int ret;
	struct req_context req = {
		.cb = align_failed_callback,
		/* not 4k align */
		.offset = 1,
	};

	/* align failed */
	ret = ocf_get(&req);
	assert_int_equal(ret, 0);
	ret = ocf_lookup(&req);
	assert_int_equal(ret, 0);

	/* failed because of NULL */
	ret = ocf_get(NULL);
	assert_int_equal(ret, -1003);
	ret = ocf_put(NULL);
	assert_int_equal(ret, -1003);
	ret = ocf_lookup(NULL);
	assert_int_equal(ret, -1003);
	ret = ocf_range_invalid(NULL);
	assert_int_equal(ret, -1003);
	ret = ocf_region_invalid(NULL);
	assert_int_equal(ret, -1003);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(engine_uc_test),
		cmocka_unit_test(ocf_submit_failed_test),
		cmocka_unit_test(ocf_exit_test),
    };

    print_message("Unit test for ocf_engine_prepare_clines\n");

    return cmocka_run_group_tests(tests, NULL, NULL);
}
