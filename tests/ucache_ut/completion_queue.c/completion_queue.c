/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: completion queue test
 * Create: 2024-02-27
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ocf_adaptor_queue.h"
#include "completion_queue.h"

static void completion_queue_push_test(void **state)
{
	completion_queue_t q;
	int ret;
	ret = completion_queue_create(&q);
	assert_int_equal(ret, 0);

	struct cq_entry e = {
		.ret = 1,
		.node = {
			.next = NULL,
			.prev = NULL,
		}
	};

	struct cq_entry e1 = {
		.ret = 2,
		.node = {
			.next = NULL,
			.prev = NULL,
		}
	};
	cq_entry_t e_ret[3] = {NULL, NULL, NULL};

	/* push element */
	completion_queue_push(q, &e);
	completion_queue_push(q, &e1);
	/* pop and check it */
	ret = completion_queue_pop(q, e_ret, 1);
	assert_int_equal(ret, 1);
	assert_non_null(e_ret[0]);
	assert_int_equal(e_ret[0]->ret, e.ret);
	
	e_ret[0] = NULL;
	/* pop elements that exceed the length of the queue */
	ret = completion_queue_pop(q, e_ret, 3);
	assert_int_equal(ret, 1);
	assert_non_null(e_ret[0]);
	assert_int_equal(e_ret[0]->ret, e1.ret);
	/* check NULL */
	assert_null(e_ret[1]);

	/* pop NULL */
	ret = completion_queue_pop(q, NULL, 3);
	assert_int_equal(ret, 0);

	/* free */
	completion_queue_put(q, 1);
}


int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(completion_queue_push_test),
    };

    print_message("Unit test for completion_queue\n");

    return cmocka_run_group_tests(tests, NULL, NULL);
}
