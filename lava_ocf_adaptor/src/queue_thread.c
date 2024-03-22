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

/* helper class to store all synchronization related objects */
struct queue_thread
{
	/* thread running the queue */
	pthread_t thread;
	/* request thread to exit */
	bool stop;
	/* associated OCF queue num */
	uint16_t queue_num;
	/* alive queue num */
	uint16_t alive_queue_num;
	/* associated OCF queue */
	struct ocf_queue *io_queues[MAX_QUEUE_NUM];
};

struct queue_thread *queue_thread_init()
{
	struct queue_thread *qt = malloc(sizeof(*qt));
	if (!qt) {
		return NULL;
	}
	qt->stop = true;
	qt->queue_num = 0;
	qt->alive_queue_num = 0;

	return qt;
}

void queue_thread_add_queue(struct queue_thread *qt, struct ocf_queue *queue)
{
	qt->io_queues[qt->queue_num] = queue;
	qt->queue_num++;
	qt->alive_queue_num++;
}

int queue_thread_run(struct queue_thread *qt, int cpu)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	int ret = pthread_create(&qt->thread, NULL, run, qt);
	pthread_setaffinity_np(qt->thread, sizeof(cpu_set_t), &mask);

	return ret;
}

void queue_thread_deinit(struct queue_thread *qt)
{
	if (!qt) {
		return;
	}

	free(qt);
}

void queue_thread_destroy(struct queue_thread *qt)
{
	if (!qt) {
		return;
	}

	if (!qt->stop) {
		/* the run thread is stopped before the first queue is deregistered,
		 * the requests of other queues may not be completely processed
		 * to ensure that all incoming requests are processed, queue destruction is performed before free(qt)
		 */
		qt->stop = true;
		pthread_join(qt->thread, NULL);
	}

	if (--qt->alive_queue_num) {
		return;
	}

	free(qt);
}

/* queue thread main function */
static void* run(void *arg)
{
	int i;
	struct queue_thread *qt = arg;
	struct ocf_queue **io_queues = qt->io_queues;
	uint16_t queue_num = qt->queue_num;
	uint32_t pending_io = 0;
	qt->stop = false;

	while (likely(!qt->stop)) {
		for (i = 0; i < queue_num; ++i) {
			pending_io += ocf_queue_pending_io(io_queues[i]);
		}

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
	}

	pthread_exit(0);
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
int initialize_threads(struct ocf_queue **io_queues,
	uint16_t queue_num, uint16_t cpu_core_num, __uint128_t core_mask)
{
	uint8_t cpu_valid_core[MAX_QUEUE_NUM];
	struct queue_thread* io_queue_threads[MAX_QUEUE_NUM];

	int ret = select_valid_cpu_core(core_mask, cpu_valid_core);
	if (ret < cpu_core_num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "core_mask valid cpu core not enough, ret is %d\n", ret);
		return ret;
	}

	for (int i = 0; i < cpu_core_num; ++i) {
		io_queue_threads[i] = queue_thread_init();
		if (io_queue_threads[i]) {
			continue;
		}
		ocf_adaptor_log(OCF_LOG_ERROR, "io_queue_thread%d init failed.\n", i);
		for (int j = 0; j < i; ++j) {
			queue_thread_deinit(io_queue_threads[j]);
		}
		return -1;
	}

	for (int i = 0; i < queue_num; ++i) {
		int t = i % cpu_core_num;
		queue_thread_add_queue(io_queue_threads[t], io_queues[i]);
		ocf_queue_set_priv(io_queues[i], io_queue_threads[t]);
	}

	for (int i = 0; i < cpu_core_num; ++i) {
		ret = queue_thread_run(io_queue_threads[i], cpu_valid_core[i]);
		if (ret) {
			ocf_adaptor_log(OCF_LOG_ERROR, "failed to start io_queue_thread%d.\n", i);
			return -1;
		}
	}

	return 0;
}

/* callback for OCF to stop the queue thread */
void queue_thread_stop(struct ocf_queue *q)
{
	struct queue_thread *qt = ocf_queue_get_priv(q);

	queue_thread_destroy(qt);
}
