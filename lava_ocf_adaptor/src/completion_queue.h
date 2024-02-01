/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __COMPLETION_QUEUE_H__
#define __COMPLETION_QUEUE_H__

#include "ocf_env.h"

struct completion_queue {
	struct list_head io_list;
	env_atomic io_no;
	env_atomic ref_count;
	env_spinlock io_list_lock;
} __attribute__((__aligned__(64)));

struct cq_entry {
	int ret;
	struct list_head node;
};

typedef struct completion_queue *completion_queue_t;
typedef struct cq_entry *cq_entry_t;

int completion_queue_create(completion_queue_t *cpl_queue);

void completion_queue_get(completion_queue_t q);

void completion_queue_put(completion_queue_t q, int i);

void completion_queue_push(completion_queue_t q, cq_entry_t entry);

int completion_queue_pop_batch(completion_queue_t q, cq_entry_t *entrys, int num);

void *get_req_context(cq_entry_t entry);
#endif