/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: string buffer test
 * Create: 2024-02-27
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <syslog.h>
#include <stdio.h>
#include "log.h"

static void logger_test(void **state)
{
	int ret;
	set_log_print(get_log_print());
	ret = log_raw(OCF_LOG_INFO, "test log\n");
	assert_int_equal(ret, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(logger_test),
    };

    print_message("Unit test for log\n");

    return cmocka_run_group_tests(tests, NULL, NULL);
}
