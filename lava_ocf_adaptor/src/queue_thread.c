/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ocf/ocf.h>
#include "queue_thread.h"
#include "log.h"

#include <stdlib.h>
#include <sched.h>
#include <pthread.h>

/* queue thread main function */
static void* run(void *);

static void* timed_trigger(void *);

/* helper class to store all synchronization related objects */
struct queue_thread
{
	/* thread running the queue */
	pthread_t thread;
	/* thread attr to restrict CPU affinity */
	pthread_attr_t attr;
	/* kick sets true, queue thread sets to false */
	bool signalled;
	/* request thread to exit */
	bool stop;
	/* conditional variable to sync queue thread and kick thread */
	pthread_cond_t cv;
	/* mutex for variables shared across threads */
	pthread_mutex_t mutex;
	/* associated OCF queue num */
	uint16_t queue_num;
	/* alive queue num */
	uint16_t alive_queue_num;
	/* associated OCF queue */
	struct ocf_queue *io_queues[MAX_QUEUE_NUM];
};

struct queue_thread *queue_thread_init(struct ocf_queue **io_queues, 
	int start, int end, int cpu)
{
	struct queue_thread *qt = malloc(sizeof(*qt));
	cpu_set_t mask;
	int ret;
	int i;

	if (!qt)
		return NULL;

	ret = pthread_cond_init(&qt->cv, NULL);
	if (ret)
		goto err_mem;

	ret = pthread_mutex_init(&qt->mutex, NULL);
	if (ret)
		goto err_cond;

	ret = pthread_attr_init(&qt->attr);
	if (ret)
		goto err_attr;
	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	qt->signalled = false;
	qt->stop = false;
	qt->queue_num = end - start + 1;
	qt->alive_queue_num = qt->queue_num;
	for (i = 0; i < qt->queue_num; ++i) {
		qt->io_queues[i] = io_queues[start + i];
	}

	/* Bind CPU */
	pthread_attr_setaffinity_np(&qt->attr, sizeof(mask), &mask);
	/* create io_queue thread */
	ret = pthread_create(&qt->thread, NULL, run, qt);
	if (ret)
		goto err_mutex;

	pthread_t kick_thread;
	ret = pthread_create(&kick_thread, NULL, timed_trigger, qt);
	if (ret)
		goto err_mutex;

	return qt;

err_mutex:
	pthread_mutex_destroy(&qt->mutex);
err_attr:
	pthread_attr_destroy(&qt->attr);
err_cond:
	pthread_cond_destroy(&qt->cv);
err_mem:
	free(qt);

	return NULL;
}

void queue_thread_signal(struct queue_thread *qt, bool stop)
{
	pthread_mutex_lock(&qt->mutex);
	qt->signalled = true;
	qt->stop = stop;
	pthread_cond_signal(&qt->cv);
	pthread_mutex_unlock(&qt->mutex);
}

void queue_thread_destroy(struct queue_thread *qt)
{
	if (!qt)
		return;

	queue_thread_signal(qt, true);
	if (--qt->alive_queue_num != 0) {
		return;
	}
	pthread_join(qt->thread, NULL);

	pthread_mutex_destroy(&qt->mutex);
	pthread_cond_destroy(&qt->cv);
	free(qt);
}

/* queue thread main function */
static void* run(void *arg)
{
	int i;
	struct queue_thread *qt = arg;
	struct ocf_queue **io_queues = qt->io_queues;
	uint8_t queue_num = qt->queue_num;
	uint32_t pending_io = 0;

	pthread_mutex_lock(&qt->mutex);

	while (!qt->stop) {
		if (qt->signalled) {
			qt->signalled = false;
			pthread_mutex_unlock(&qt->mutex);

			for (i = 0; i < queue_num; ++i)
				pending_io += ocf_queue_pending_io(io_queues[i]);

			/* execute items on the queue */
			i = 0;
			while (pending_io > 0) {
				if (ocf_queue_pending_io(io_queues[i]) > 0) {
					ocf_queue_run_single(io_queues[i]);
					--pending_io;
				}
				i = (i + 1) % queue_num;
			}

			PollCompletion(1024);

			pthread_mutex_lock(&qt->mutex);
		}

		if (!qt->stop && !qt->signalled) 
			pthread_cond_wait(&qt->cv, &qt->mutex);
	}

	pthread_mutex_unlock(&qt->mutex);

	pthread_exit(0);
}

static void* timed_trigger(void *arg)
{
	struct queue_thread *qt = arg;
	while (1) {
		queue_thread_signal(qt, false);
		sleep(1);
	}
	return NULL;
}

static int select_valid_cpu_core(__uint128_t core_mask, uint8_t *cpu_valid_core)
{
	int i = 0;
	uint8_t idx = 0;

	for (; idx < 128; ++idx) {
		if (((__uint128_t)1 << (idx)) & core_mask) {
			cpu_valid_core[i++] = idx;
		}
	}

	return i;
}

/* initialize I/O queue and management queue thread */
int initialize_threads(struct ocf_queue *mngt_queue, struct ocf_queue **io_queues,
	uint16_t queue_num, uint16_t cpu_core_num, __uint128_t core_mask)
{
	int i;
	int ret = 0;
	uint8_t cpu_valid_core[MAX_QUEUE_NUM];
	struct queue_thread* io_queue_threads[MAX_QUEUE_NUM];
	uint16_t thread_handle_q_num = queue_num / cpu_core_num;
	uint16_t remain_q = queue_num % cpu_core_num;
	uint16_t start = 0;
	uint16_t end;

	ret = select_valid_cpu_core(core_mask, cpu_valid_core);
	if (ret < cpu_core_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "core_mask valid cpu core not enough, ret is %d\n", ret);
		return ret;
	}

	struct queue_thread* mngt_queue_thread = queue_thread_init(&mngt_queue, 0, 0, cpu_valid_core[0]);
	if (!mngt_queue_thread) {
		ocf_adaptor_log(OCF_LOG_ERROR, "mngt_queue_thread init failed.\n");
		return -1;
	}
	ocf_queue_set_priv(mngt_queue, mngt_queue_thread);

	for (i = 0; i < cpu_core_num; ++i) {
		end = start + (thread_handle_q_num - 1 + (remain_q ? 1 : 0));
		io_queue_threads[i] = queue_thread_init(io_queues,
			start, end, cpu_valid_core[i]);
		if (!io_queue_threads[i]) {
			ocf_adaptor_log(OCF_LOG_ERROR, "io_queue_thread%d init failed.\n", i);
			break;
		}
		for (int j = start; j <= end; ++j) {
			ocf_queue_set_priv(io_queues[j], io_queue_threads[i]);
		}
		if (remain_q) {
			--remain_q;
		}
		start = end + 1;
	}

	if (i != cpu_core_num) {
		queue_thread_destroy(mngt_queue_thread);
		while (i > 0) {
			queue_thread_destroy(io_queue_threads[i--]);
		}
		return -1;
	}

	return 0;
}

/* callback for OCF to kick the queue thread */
void queue_thread_kick(struct ocf_queue *q)
{
	struct queue_thread *qt = ocf_queue_get_priv(q);

	queue_thread_signal(qt, false);
}

/* callback for OCF to stop the queue thread */
void queue_thread_stop(struct ocf_queue *q)
{
	struct queue_thread *qt = ocf_queue_get_priv(q);

	queue_thread_destroy(qt);
}
