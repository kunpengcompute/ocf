#include <functional>
#include <vector>
#include <queue>
#include <cstring>
#include <thread>
#include <iostream>
#include "device.h"
#include "ocf/ocf.h"
#include "ocf/ocf_def.h"
#include "pthread.h"

using namespace std;

#define CHUNK_SIZE (128 * MiB)
#define MAX_CHUNK_NUMS (CACHE_MAX_SUPPORT_IN_TIB * TiB / CHUNK_SIZE)

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
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

int AllocChunks(std::size_t num, std::vector<uint64_t> *chunk_ids)
{
	std::size_t cnt = 0;
	for (uint64_t i = 0; i < MAX_CHUNK_NUMS && cnt < num; ++i) {
		if (g_chunks[i]) {
			continue;
		}
		Chunk *ck = (Chunk*)malloc(sizeof(Chunk));
		if (!ck) {
			return -1;
		}
		ck->ptr = (char*)malloc(CHUNK_SIZE);
		if (!ck->ptr) {
			return -1;
		}
		ck->size = CHUNK_SIZE;
		ck->chunk_id = i;
		chunk_ids->push_back(i);
		g_chunks[i] = ck;
		++cnt;
	}
	
	if (cnt < num) {
		FreeChunks(chunk_ids);
		return -1;
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
		return -1;
	std::memcpy(ck->ptr + segment->offset, segment->data, segment->length);
	std::this_thread::sleep_for(std::chrono::microseconds(g_cache_io_time));
	return 0;
}

int Read(uint64_t chunk_id, Segment_t segment)
{
	Chunk_t ck = g_chunks[chunk_id];
	if (!ck || !ck->ptr || !segment || !segment->data)
		return -1;
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
	pthread_mutex_lock(&queue_mutex);
	g_queue.push(req);
	pthread_mutex_unlock(&queue_mutex);
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
	pthread_mutex_lock(&queue_mutex);
	g_queue.push(req);
	pthread_mutex_unlock(&queue_mutex);
	return 0;
}

int PollCompletion(uint32_t max)
{
	uint32_t cnt = 0;
	while (cnt < max) {
		Request_t req;
		pthread_mutex_lock(&queue_mutex);
		if (g_queue.empty()) {
			pthread_mutex_unlock(&queue_mutex);
			break;
		}
		req = g_queue.front();
		g_queue.pop();
		pthread_mutex_unlock(&queue_mutex);
		++cnt;
		req->cb(0, req);
	}
	
	return cnt;
}
