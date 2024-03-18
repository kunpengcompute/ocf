/*
 * Copyright(c) 2021-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __MNGT_QUEUE_THREAD_H__
#define __MNGT_QUEUE_THREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

int initialize_mngt_threads(struct ocf_queue *mngt_queue);
void mqueue_thread_kick(struct ocf_queue *q);
void mqueue_thread_stop(struct ocf_queue *q);

#ifdef __cplusplus
}
#endif
#endif