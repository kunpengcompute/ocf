/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE

#include <ocf/ocf.h>
#include <ocf/ocf_err.h>
#include <../../src/ocf_cache_priv.h>
#include <../../src/ocf_request.h>
#include <../../src/concurrency/ocf_cache_line_concurrency.h>
#include <../../src/engine/engine_inv.h>

#include "check_queue_thread.h"
#include "ocf_queue_utils.h"
#include "log.h"

#include <stdlib.h>
#include <sched.h>
#include <pthread.h>

/* 超时返回已经拿到锁的io */
static void do_timeout_process(struct ocf_request **req, uint32_t cnt) {
	uint8_t prev;
	
	for (uint32_t i = 0; i < cnt; i++) {
		// 超时返回
		ocf_io_end(&req[i]->ioi.io, -OCF_ERR_TIMEOUT_IO);
		// 根据io类型进行处理，write: invalid，read: unlock
		if (req[i]->rw == OCF_WRITE) {
			prev = env_atomic8_cmpxchg(&(req[i]->is_invalided), 0, 1);
			if (prev == 0) { /* req has not been invalided */
				ocf_engine_invalidate_without_flush(req[i]);
			}
		} else if (req[i]->rw == OCF_READ) {
			prev = env_atomic8_cmpxchg(&(req[i]->is_invalided), 0, 1);
			if (prev == 0) { /* req has not been unlock */
				struct ocf_alock *c = req[i]->cache->device->concurrency.cache_line;
				ocf_req_unlock(c, req[i]);
			}
		} else {
			ocf_adaptor_log(OCF_LOG_ERROR, "invalid timeout req rw: %d\n", req[i]->rw);
		}

		ocf_req_put(req[i]);
	}
	
}

static void do_check(check_queue_t q, uint32_t max_check)
{
	struct ocf_request *req;
	struct list_head *now;
	struct list_head *nxt;
	uint32_t cnt = 0;
	uint32_t timeout_cnt = 0;
	uint32_t ignore_cnt = 0;

	uint8_t is_ended;
	uint64_t now_time = env_get_tick_count();
	uint64_t past_time;
	struct ocf_request *timeout_reqs[max_check]; // todo: 一次最多处理x个超时io

	/* LOCK */
	env_spinlock_lock(&q->io_list_lock);

	/* 首先判断io是否已经结束，最多判断max_check个已结束的io */
	/* 如果io没有结束，判断io是否超时，是否已经拿到锁，若没有超时则结束判断 */
	/* 若出现io超时，并且已经拿到锁，将io放入数组中，后续函数超时返回 */
	/* 若没有拿到锁，超时阈值超过ocf异常阈值，设置ocf状态异常 */
	list_for_each_safe(now, nxt, &q->io_list) {
		/* Get the first request */
		req = list_first_entry(&q->io_list, struct ocf_request, check_list);
		is_ended = env_atomic8_read(&req->ioi.io.is_ended);
		if (is_ended) {
			list_del(now);
			ocf_req_put(req);
		} else {
			past_time = now_time - req->ocf_start_timestamp;
			if (past_time < get_ocf_global_status()) {
				break;
			} else { 
				if (req->ready_to_cache) {
					timeout_reqs[timeout_cnt++] = req;
					list_del(now);
				} else {
					ignore_cnt++;
				}
			} // todo: 设置ocf状态异常
		}
		if (++cnt >= max_check) {
			break;
		}
	}
	
	env_atomic_sub(cnt - ignore_cnt, &q->io_no);
	/* UNLOCK */
	env_spinlock_unlock(&q->io_list_lock);

	if (!timeout_cnt) {
		return;
	}

	do_timeout_process(timeout_reqs, timeout_cnt);
}

static uint32_t check_queue_pending_io(check_queue_t q)
{
	return env_atomic_read(&q->io_no);
}

/* check queue thread main function */
static void* check_run(void *arg)
{
	int i;
	struct check_queue_thread *qt = arg;
	check_queue_t *io_queues = qt->io_queues;
	uint16_t queue_num = qt->queue_num;
	uint32_t pending_io = 0;
	qt->stop = false;

	while (!qt->stop) {
		// todo: do something
		for (i = 0; i < queue_num; ++i) {
			pending_io = check_queue_pending_io(io_queues[i]);
			if (pending_io > 0) {
				do_check(io_queues[i], pending_io);
			}
		}
        usleep(500000);
	}

	pthread_exit(0);
}

int check_queue_thread_run(struct check_queue_thread *qt, int cpu)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	int ret = pthread_create(&qt->thread, NULL, check_run, qt);
	pthread_setaffinity_np(qt->thread, sizeof(cpu_set_t), &mask);

	return ret;
}

int check_queue_create(check_queue_t *check_queue)
{
	check_queue_t tmp_queue;
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

	*check_queue = tmp_queue;

	return 0;
}

void *ocf_check_queue_get_priv(check_queue_t q)
{
	return q->priv;
}

void ocf_check_queue_set_priv(check_queue_t q, void *priv)
{
	q->priv = priv;
}

void check_queue_thread_destroy(struct check_queue_thread *qt)
{
	if (!qt) {
		return;
	}

	if (!qt->stop) {
		qt->stop = true;
		pthread_join(qt->thread, NULL);
	}

	if (--qt->alive_queue_num) {
		return;
	}

	free(qt);
}

void check_queue_thread_stop(check_queue_t q)
{
	struct check_queue_thread *qt = ocf_check_queue_get_priv(q);

	check_queue_thread_destroy(qt);
}

void check_queue_get(check_queue_t q)
{
	env_atomic_inc(&q->ref_count);
}

void check_queue_put(check_queue_t q)
{
	if (env_atomic_dec_return(&q->ref_count) == 0) {
		check_queue_thread_stop(q);
		env_spinlock_destroy(&q->io_list_lock);
		env_free(q);
	}
}

struct check_queue_thread *check_queue_thread_init()
{
	struct check_queue_thread *qt = malloc(sizeof(*qt));
	if (!qt) {
		return NULL;
	}
	qt->stop = true;
	qt->queue_num = 0;
	qt->alive_queue_num = 0;

	return qt;
}

void check_queue_thread_deinit(struct check_queue_thread *qt)
{
	if (!qt) {
		return;
	}

	free(qt);
}

void check_thread_add_queue(struct check_queue_thread *qt, check_queue_t queue)
{
	qt->io_queues[qt->queue_num] = queue;
	qt->queue_num++;
	qt->alive_queue_num++;
}

int initialize_check_threads(check_queue_t *check_queues,
    uint16_t queue_num, uint16_t cpu_core_num, __uint128_t core_mask)
{
	uint8_t cpu_valid_core[MAX_QUEUE_NUM];
	struct check_queue_thread* check_queue_threads[MAX_QUEUE_NUM];

	int ret = select_valid_cpu_core(core_mask, cpu_valid_core);
	if (ret < cpu_core_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "core_mask valid cpu core not enough, ret is %d\n", ret);
		return ret;
	}

	for (int i = 0; i < cpu_core_num; ++i) {
		check_queue_threads[i] = check_queue_thread_init();
		if (check_queue_threads[i]) {
			continue;
		}
		ocf_adaptor_log(OCF_LOG_ERROR, "check_queue_thread%d init failed.\n", i);
		for (int j = 0; j < i; ++j) {
			check_queue_thread_deinit(check_queue_threads[j]);
		}
		return -1;
	}

	for (int i = 0; i < queue_num; ++i) {
		int t = i % cpu_core_num;
		check_thread_add_queue(check_queue_threads[t], check_queues[i]);
		ocf_check_queue_set_priv(check_queues[i], check_queue_threads[t]);
	}

	for (int i = 0; i < cpu_core_num; ++i) {
		ret = check_queue_thread_run(check_queue_threads[i], cpu_valid_core[i]);
		if (ret) {
			ocf_adaptor_log(OCF_LOG_ERROR, "failed to start check_queue_thread%d.\n", i);
			return -1;
		}
	}

	return 0;
}

void check_queue_push(check_queue_t q, struct ocf_request *req)
{
	ocf_req_get(req);
	INIT_LIST_HEAD(&req->check_list);

	env_spinlock_lock(&q->io_list_lock);
	list_add_tail(&req->check_list, &q->io_list);
	env_atomic_inc(&q->io_no);
	env_spinlock_unlock(&q->io_list_lock);
}
