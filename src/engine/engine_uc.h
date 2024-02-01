/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: new cache mode engine: UCache interface
 * Create: 2024-01-27
 */

#ifndef ENGINE_UC_H_
#define ENGINE_UC_H_

int ocf_read_ucache(struct ocf_request *req);

int ocf_write_ucache(struct ocf_request *req);

#endif /* ENGINE_UC_H_ */
