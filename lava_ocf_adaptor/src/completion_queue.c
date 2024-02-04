/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ocf_adaptor_queue.h"
#include "completion_queue.h"


int completion_queue_create(completion_queue_t *cpl_queue)
{
	completion_queue_t tmp_queue;
	int result;

	tmp_queue = env_zalloc(sizeof(*tmp_queue), ENV_MEM_NORMAL);
	if (!tmp_queue) {
		return -OCF_ERR_NO_MEM;
	}

	env_atomic_set(&tmp_queue->io_no, 0);
	result = env_spinlock_init(&tmp_queue->io_list_lock);
	if (result) {
		env_free(tmp_queue);
		return result;
	}

	INIT_LIST_HEAD(&tmp_queue->io_list);
	env_atomic_set(&tmp_queue->ref_count, 1);

	*cpl_queue = tmp_queue;

	return 0;
}


void completion_queue_get(completion_queue_t q)
{
	env_atomic_inc(&q->ref_count);
}

void completion_queue_put(completion_queue_t q, int i)
{
	if (!env_atomic_sub_return(i, &q->ref_count)) {
		env_spinlock_destroy(&q->io_list_lock);
		env_free(q);
	}
}

void completion_queue_push(completion_queue_t q, cq_entry_t entry)
{
	INIT_LIST_HEAD(&entry->node);
	completion_queue_get(q);
	env_spinlock_lock(&q->io_list_lock);
	list_add_tail(&entry->node, &q->io_list);
	env_atomic_inc(&q->io_no);
	env_spinlock_unlock(&q->io_list_lock);
}

int completion_queue_pop_batch(completion_queue_t q, cq_entry_t *entrys, int num)
{
	if (!entrys) {
		return 0;
	}
	struct list_head *now;
	struct list_head *nxt;
	int cnt = 0;
	env_spinlock_lock(&q->io_list_lock);
	list_for_each_safe(now, nxt, &q->io_list) {
		entrys[cnt] = (cq_entry_t)list_entry(now, struct cq_entry, node);
		list_del(now);
		if (++cnt >= num) {
			break;
		}
	}
	env_atomic_sub(cnt, &q->io_no);
	env_spinlock_unlock(&q->io_list_lock);
	completion_queue_put(q, cnt);
	return cnt;
}

void *get_req_context(cq_entry_t entry)
{
	return list_entry(entry, struct req_context, internal);
}