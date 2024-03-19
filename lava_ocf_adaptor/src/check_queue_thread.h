/*
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __CHECK_QUEUE_H__
#define __CHECK_QUEUE_H__

#include "ocf_env.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_QUEUE_NUM 64

struct check_queue {
    struct list_head io_list;
    void *priv;
    env_atomic io_no;
    env_atomic ref_count;
    env_spinlock io_list_lock;
} __attribute__((__aligned__(64)));

typedef struct check_queue *check_queue_t;

/* store synchronization check io */
struct check_queue_thread {
    /* thread running the queue */
    pthread_t thread;
    /* request thread to exit */
    bool stop;
    /* associated OCF queue num */
    uint16_t queue_num;
    /* alive queue_num */
    uint16_t alive_queue_num;
    /* associated OCF queue */
    struct check_queue *io_queues[MAX_QUEUE_NUM];
};

int check_queue_thread_run(struct check_queue_thread *qt, int cpu);
int check_queue_create(check_queue_t *check_queue);
void *ocf_check_queue_get_priv(check_queue_t q);
void ocf_check_queue_set_priv(check_queue_t q, void *priv);
void check_queue_thread_destroy(struct check_queue_thread *qt);
void check_queue_thread_stop(check_queue_t q);
void check_queue_get(check_queue_t q);
void check_queue_put(check_queue_t q);
struct check_queue_thread *check_queue_thread_init();
void check_queue_thread_deinit(struct check_queue_thread *qt);
void check_queue_thread_add_queue(struct check_queue_thread *qt, check_queue_t queue);
int initialize_check_threads(check_queue_t *check_queues,
    uint16_t queue_num, uint16_t cpu_core_num, __uint128_t core_mask);

void check_queue_push(check_queue_t q, struct ocf_request *req);

#ifdef __cplusplus
}
#endif
#endif
