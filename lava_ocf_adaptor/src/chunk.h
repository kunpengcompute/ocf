/*
 * Copyright(c) 2019-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __CHUNK_H_
#define __CHUNK_H_

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

typedef struct Segment {
	uint32_t offset;
	uint32_t length;
	char *data;
} Segment;
typedef Segment *Segment_t;

typedef struct Request {
	uint64_t chunk_id;
	std::vector<Segment> segments;
	void *user_ctx;
	std::function<void(int ret, void *context)> cb;
} Request;
typedef Request *Request_t;

/* 申请释放chunk */
int AllocChunks(std::size_t num, std::vector<uint64_t> *chunk_ids);
int FreeChunks(std::vector<uint64_t> *chunk_ids);

/* 异步读写操作 */
int AioWrite(Request_t req);
int AioRead(Request_t req);

extern "C" int PollChunkCompletion(uint32_t max);

#endif