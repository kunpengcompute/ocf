/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: OCF适配DAS新增PF预取模式接口
 * Create: 2023-08-16
 */

#ifndef ENGINE_PF_H_
#define ENGINE_PF_H_

int ocf_prefetch(struct ocf_request *req);

int ocf_prefetch_fast(struct ocf_request *req);

#endif /* ENGINE_PF_H_ */