#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <cstdint>
#include <cstdlib>
#include <vector>

#define THREAD_NUMS 4 /* 读写处理线程的数量 */
#define ALLOC_CHUNK_ERR 1
#define WRITE_ERR 1
#define READ_ERR 1

enum {
    DEVICE_READ,
    DEVICE_WRITE,
};

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

typedef struct {
    Request_t req;
    uint8_t opc;
    uint8_t status;
} AioRequest;

/* 申请释放chunk */
int AllocChunks(std::size_t num, std::vector<uint64_t> *chunk_ids);
int FreeChunks(std::vector<uint64_t> *chunk_ids);

/* 同步读写操作 */
int Write(uint64_t chunk_id, Segment_t segment);
int Read(uint64_t chunk_id, Segment_t segment);

/* 异步读写操作 */
int AioWrite(Request_t req);
int AioRead(Request_t req);

extern "C" int PollCompletion(uint32_t max);

#endif