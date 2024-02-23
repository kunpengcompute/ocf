/*
 * Copyright(c) 2021-2021 Intel Corporation
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ut for engine_uc file
 * Author: hebo
 * Create: 2024-02-19
 */

#pragma once

int initialize_threads(struct ocf_queue *mngt_queue, struct ocf_queue *io_queue);
void queue_thread_kick(ocf_queue_t q);
void queue_thread_stop(ocf_queue_t q);
