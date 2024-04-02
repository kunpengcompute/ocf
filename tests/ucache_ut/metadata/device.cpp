#include <functional>
#include <vector>
#include <queue>
#include <cstring>
#include <thread>
#include <iostream>
#include "device.h"
#include "ocf/ocf.h"
#include "ocf/ocf_def.h"
#include "ocf_lru_structs.h"
#include "pthread.h"

using namespace std;

#define CHUNK_SIZE (128 * MiB)
#define FAKE_CHUNK_SIZE (64 * KiB) // we donnot need to malloc true size for cache
#define MAX_CHUNK_NUMS (CACHE_MAX_SUPPORT_IN_TB * TiB / CHUNK_SIZE)

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
static uint64_t g_cache_io_time = 5;
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
			return ALLOC_CHUNK_ERR;
		}
		ck->ptr = (char*)malloc(FAKE_CHUNK_SIZE);
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
		printf("We could not alloced %d chunks less than %d chunks, max chunks is %d \n", cnt, num, MAX_CHUNK_NUMS);
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

int AioWrite(Request_t req)
{
	pthread_mutex_lock(&queue_mutex);
	g_queue.push(req);
	pthread_mutex_unlock(&queue_mutex);
	return 0;
}

int AioRead(Request_t req)
{
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
