/*
 * Copyright(c) 2021-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ocf/ocf.h>
#include "mngt_queue_thread.h"
#include "log.h"

#include <stdlib.h>
#include <sched.h>
#include <pthread.h>

/* queue thread main function */
static void* run(void *);

struct mqueue_thread
{
	/* thread running the queue */
	pthread_t thread;
	/* kick sets true, queue thread sets to false */
	bool signalled;
	/* request thread to exit */
	bool stop;
	/* conditional variable to sync queue thread and kick thread */
	pthread_cond_t cv;
	/* mutex for variables shared across threads */
	pthread_mutex_t mutex;
	/* associated OCF queue */
	struct ocf_queue *queue;
};

struct mqueue_thread *mqueue_thread_init(struct ocf_queue *q)
{
	struct mqueue_thread *qt = malloc(sizeof(*qt));
	if (!qt) {
		return NULL;
	}

	int ret = pthread_cond_init(&qt->cv, NULL);
	if (ret) {
		goto err_mem;
	}

	ret = pthread_mutex_init(&qt->mutex, NULL);
	if (ret) {
		goto err_cond;
	}

	qt->signalled = false;
	qt->stop = false;
	qt->queue = q;

	ret = pthread_create(&qt->thread, NULL, run, qt);
	if (ret) {
		goto err_mutex;
	}

	return qt;

err_mutex:
	pthread_mutex_destroy(&qt->mutex);
err_cond:
	pthread_cond_destroy(&qt->cv);
err_mem:
	free(qt);

	return NULL;
}

void mqueue_thread_signal(struct mqueue_thread *qt, bool stop)
{
	pthread_mutex_lock(&qt->mutex);
	qt->signalled = true;
	qt->stop = stop;
	pthread_cond_signal(&qt->cv);
	pthread_mutex_unlock(&qt->mutex);
}

void mqueue_thread_destroy(struct mqueue_thread *qt)
{
	if (!qt)
		return;

	mqueue_thread_signal(qt, true);
	pthread_join(qt->thread, NULL);

	pthread_mutex_destroy(&qt->mutex);
	pthread_cond_destroy(&qt->cv);
	free(qt);
}

/* queue thread main function */
static void* run(void *arg)
{
	struct mqueue_thread *qt = arg;
	struct ocf_queue *q = qt->queue;

	pthread_mutex_lock(&qt->mutex);

	while (!qt->stop) {
		if (qt->signalled) {
			qt->signalled = false;
			pthread_mutex_unlock(&qt->mutex);

			/* execute items on the queue */
			ocf_queue_run(q);

			pthread_mutex_lock(&qt->mutex);
		}

		if (!qt->stop && !qt->signalled)
			pthread_cond_wait(&qt->cv, &qt->mutex);
	}

	pthread_mutex_unlock(&qt->mutex);

	pthread_exit(0);
}

/* initialize management queue thread */
int initialize_mngt_threads(struct ocf_queue *mngt_queue)
{
	struct mqueue_thread* mngt_queue_thread = mqueue_thread_init(mngt_queue);
	if (!mngt_queue_thread) {
		return -1;
	}

	ocf_queue_set_priv(mngt_queue, mngt_queue_thread);
	return 0;
}

/* callback for OCF to kick the queue thread */
void mqueue_thread_kick(ocf_queue_t q)
{
	struct mqueue_thread *qt = ocf_queue_get_priv(q);
	mqueue_thread_signal(qt, false);
}

/* callback for OCF to stop the queue thread */
void mqueue_thread_stop(ocf_queue_t q)
{
	struct mqueue_thread *qt = ocf_queue_get_priv(q);
	mqueue_thread_destroy(qt);
}
