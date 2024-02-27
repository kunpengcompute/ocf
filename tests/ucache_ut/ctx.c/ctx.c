/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ocf context test
 * Create: 2024-02-27
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <ocf/ocf.h>
#include <stdio.h>
#include "log.h"
#include "volume.h"
#include "ctx.h"
#include "ocf_ctx_priv.h"

#define MAX_BUF 1024
#define PAGE_SIZE 4096

int __wrap_volume_init(ocf_ctx_t ctx)
{
	return 0;
}

void __wrap_volume_cleanup(ocf_ctx_t ctx)
{
}

static ocf_ctx_t test_ctx;

static void ctx_init_test(void **state)
{
	int ret;
	test_ctx = NULL;
	ret = ctx_init(&test_ctx);
	assert_int_equal(ret, 0);
	assert_non_null(test_ctx);
}

static void ctx_rw_test(void **state)
{
	ctx_data_t *data = NULL;
	uint32_t pages = 1;
	char buffer[20];
	char compare_buffer[20];
	memset(buffer, 0, 20);
	memset(compare_buffer, 0, 20);

	data = ctx_data_alloc(test_ctx, pages);
	assert_non_null(data);

	/* first fill the buffer with zero */
	ctx_data_zero_check(test_ctx, data, pages*PAGE_SIZE);
	/* seek to begin */
	ctx_data_seek_check(test_ctx, data, ctx_data_seek_begin, 0);

	/* read empty */
	ctx_data_rd_check(test_ctx, buffer, data, 20);
	assert_memory_equal(buffer, compare_buffer, 20);

	/* write something */
	memcpy(compare_buffer, "hello io", 8);
	ctx_data_seek_check(test_ctx, data, ctx_data_seek_begin, 0);
	ctx_data_wr_check(test_ctx, data, compare_buffer, 8);
	/* read to check */
	ctx_data_seek_check(test_ctx, data, ctx_data_seek_begin, 0);
	ctx_data_rd_check(test_ctx, buffer, data, 8);
	assert_memory_equal(buffer, compare_buffer, 8);

	/* fill buffer for copy test */
	memcpy(compare_buffer, "llo io", 6);
	/* copy data to data_cpy */
	ctx_data_t *data_cpy = ctx_data_alloc(test_ctx, pages);
	assert_non_null(data_cpy);
	ctx_data_cpy(test_ctx, data_cpy, data, 0, 2, 6);
	/* equal test */
	ctx_data_rd_check(test_ctx, buffer, data_cpy, 6);
	assert_memory_equal(buffer, compare_buffer, 6);

	/* test seek current */
	ctx_data_seek_check(test_ctx, data, ctx_data_seek_begin, 0);
	ctx_data_seek_check(test_ctx, data, ctx_data_seek_current, 2);
	ctx_data_rd_check(test_ctx, buffer, data, 6);
	assert_memory_equal(buffer, compare_buffer, 6);

	ctx_data_free(test_ctx, data);
}

static void empty_test(void **state)
{
	ctx_data_t *data = NULL;
	uint32_t pages = 1;
	int ret = 0;

	data = ctx_data_alloc(test_ctx, pages);
	assert_non_null(data);

	/* lock/unlock test */
	ret = ctx_data_mlock(test_ctx, data);
	assert_int_equal(ret, 0);
	ctx_data_munlock(test_ctx, data);
	ctx_data_secure_erase(test_ctx, data);

	/* cleaner test */
	ctx_cleaner_init(test_ctx, NULL);
	ctx_cleaner_kick(test_ctx, NULL);
	ctx_cleaner_stop(test_ctx, NULL);

	ctx_data_free(test_ctx, data);
}

static void ctx_cleanup_test(void **state)
{
	ctx_cleanup(test_ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(ctx_init_test),
		cmocka_unit_test(ctx_rw_test),
		cmocka_unit_test(empty_test),
		cmocka_unit_test(ctx_cleanup_test),
    };

    print_message("Unit test for ocf ctx\n");

    return cmocka_run_group_tests(tests, NULL, NULL);
}
