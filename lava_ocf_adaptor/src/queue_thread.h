/*
 * Copyright(c) 2021-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __QUEUE_THREAD_H__
#define __QUEUE_THREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_QUEUE_NUM 64

int initialize_threads(struct ocf_queue **io_queues,
    uint16_t queue_num, uint16_t cpu_core_num, __uint128_t core_mask);
void queue_thread_kick(struct ocf_queue *q);
void queue_thread_stop(struct ocf_queue *q);

#ifdef __cplusplus
}
#endif
#endif