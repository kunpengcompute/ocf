#include <functional>
#include <vector>
#include <queue>
#include <cstring>
#include <thread>
#include <iostream>
#include "device.h"

using namespace std;

#define CHUNK_SIZE (1 << 27)
#define MAX_CHUNK_NUMS (16)

typedef struct {
	uint64_t chunk_id;
	char *ptr;
	uint32_t size;
} Chunk;
typedef Chunk *Chunk_t;

/* chunk mock */
static Chunk_t g_chunks[MAX_CHUNK_NUMS];
/* completion queue mock */
static queue<Request_t> g_queue;
static uint64_t g_cache_io_time = 10;

int AllocChunks(std::size_t num, std::vector<uint64_t> *chunk_ids)
{
	std::size_t cnt = 0;
	for (uint64_t i = 0; i < MAX_CHUNK_NUMS && cnt < num; ++i) {
		if (g_chunks[i]) {
			continue;
		}
		Chunk *ck = (Chunk*)malloc(sizeof(Chunk));
		if (!ck) {
			return ALLOC_CHUNK_ERR;
		}
		ck->ptr = (char*)malloc(CHUNK_SIZE);
		if (!ck->ptr) {
			return ALLOC_CHUNK_ERR;
		}
		ck->size = CHUNK_SIZE;
		ck->chunk_id = i;
		chunk_ids->push_back(i);
		g_chunks[i] = ck;
		++cnt;
	}
	
	if (cnt < num) {
		FreeChunks(chunk_ids);
		return ALLOC_CHUNK_ERR;
	}
	return 0;
}

int FreeChunks(std::vector<uint64_t> *chunk_ids)
{
	for (uint64_t id : *chunk_ids) {
		if (!g_chunks[id]) {
			continue;
		}
		free(g_chunks[id]->ptr);
		g_chunks[id]->ptr = nullptr;
		free(g_chunks[id]);
		g_chunks[id] = nullptr;
	}
	
	return 0;
}

int Write(uint64_t chunk_id, Segment_t segment)
{
	Chunk_t ck = g_chunks[chunk_id];
	if (!ck || !ck->ptr || !segment || !segment->data)
		return WRITE_ERR;
	std::memcpy(ck->ptr + segment->offset, segment->data, segment->length);
	std::this_thread::sleep_for(std::chrono::microseconds(g_cache_io_time));
	return 0;
}

int Read(uint64_t chunk_id, Segment_t segment)
{
	Chunk_t ck = g_chunks[chunk_id];
	if (!ck || !ck->ptr || !segment || !segment->data)
		return READ_ERR;
	std::memcpy(segment->data, ck->ptr + segment->offset, segment->length);
	std::this_thread::sleep_for(std::chrono::microseconds(g_cache_io_time));
	return 0;
}

int AioWrite(Request_t req)
{
	uint64_t chunk_id = req->chunk_id;
	for (auto &segment : req->segments) {
		if (Write(chunk_id, &segment)) {
			return -1;
		}
	}
	/* write done, push in completion queue */
	g_queue.push(req);
	return 0;
}

int AioRead(Request_t req)
{
	uint64_t chunk_id = req->chunk_id;
	for (auto &segment : req->segments) {
		if (Read(chunk_id, &segment)) {
			return -1;
		}
	}
	/* write done, push in completion queue */
	g_queue.push(req);
	return 0;
}

int PollCompletion(uint32_t max)
{
	uint32_t cnt = 0;
	while (cnt < max) {
		Request_t req;
		if (g_queue.empty()) {
			break;
		}
		req = g_queue.front();
		g_queue.pop();
		++cnt;
		req->cb(0, req);
	}
	
	return cnt;
}
