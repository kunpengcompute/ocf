/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: string buffer test
 * Create: 2024-02-27
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "utils/utils_strbuf.h"

/* re-define macros for easy testing */
#undef DEFAULT_BUF_SIZE
#undef DEFAULT_PRINT_BUF_SIZE

#define DEFAULT_BUF_SIZE 10
#define DEFAULT_PRINT_BUF_SIZE 10

static void strbuf_write_format_str_test(void **state)
{
	struct strbuf* b = new_strbuf();
	assert_non_null(b);
	/* not need expand format buf space*/
	strbuf_write_format_str(b, "number %d", 1);
	strbuf_write_end(b);
	assert_string_equal(b->buf, "number 1");
	delete_strbuf(b);

	b = new_strbuf();
	assert_non_null(b);
	/* need expand format buf space*/
	strbuf_write_format_str(b, "number is %d", 9999);
	strbuf_write_end(b);
	assert_string_equal(b->buf, "number is 9999");
	delete_strbuf(b);
}

static void strbuf_write_str_test(void **state)
{
	struct strbuf* b = new_strbuf();
	assert_non_null(b);
	/* not need expand format buf space*/
	strbuf_write_str(b, "number 100000");
	strbuf_write_end(b);
	assert_string_equal(b->buf, "number 100000");
	delete_strbuf(b);

	b = new_strbuf();
	assert_non_null(b);
	/* not need expand format buf space*/
	strbuf_write_str(b, "number 999999999");
	strbuf_write_end(b);
	assert_string_equal(b->buf, "number 999999999");
	delete_strbuf(b);
}

static void strbuf_write_char_test(void **state)
{
	struct strbuf* b = new_strbuf();
	assert_non_null(b);
	strbuf_write_char(b, 'c');
	strbuf_write_end(b);
	assert_string_equal(b->buf, "c");
	delete_strbuf(b);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(strbuf_write_format_str_test),
		cmocka_unit_test(strbuf_write_str_test),
		cmocka_unit_test(strbuf_write_char_test),
    };

    print_message("Unit test for utils_strbuf\n");

    return cmocka_run_group_tests(tests, NULL, NULL);
}
