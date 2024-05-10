#include <cstdio>
#include <cstring>
#include <atomic>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <libaio.h>
#include <pthread.h>
#include "chunk.h"
#include "log.h"
#include "ocf_env.h"

#define AIO_REQUEST_ERR 1
#define CHUNK_SIZE (1 << 27)	// 128MiB
#define NETWORK_DELAY_US 10
#define SEC2USEC 1000000

#define AIO_DEPTH 0x80000

typedef struct {
    uint64_t chunk_id;
    uint64_t offset;		/* chunk在磁盘上的起始位置 */
    uint32_t size;
    bool allocated;			/* chunk是否已分配 */
} Chunk;					/* chunk对接磁盘 */

typedef Chunk *Chunk_t;

static int g_disk_fd;
static uint64_t g_chunk_num;			/* chunk总数 */
static Chunk_t g_chunks = nullptr;
static pthread_mutex_t g_chunk_mutex;	/* 申请chunk时加锁 */
static io_context_t g_ctx;

/* 系统调度可能导致普通sleep类函数睡眠时间不准确 */
static inline void SpinSleepUS(uint64_t sleepTime)
{
    struct timeval startTime, stopTime;
    uint64_t start, end;

    gettimeofday(&startTime, nullptr);
    start = startTime.tv_sec * SEC2USEC + startTime.tv_usec;
    do {
        gettimeofday(&stopTime, nullptr);
        end = stopTime.tv_sec * SEC2USEC + stopTime.tv_usec;
    } while (end - start < sleepTime);
}

class RequestCounter {
public:
    RequestCounter(Request *req, int max) : req_(req), max_(max), error_(0)
    {
        env_atomic_set(&curr_, 0);
    }

    bool AddAndComplete()
    {
        return env_atomic_add_return(1, &curr_) == max_;
    }

    void Callback()
    {
        req_->cb(error_, req_);
    }

public:
    Request *req_; /* 原始的Request */
    int max_;      /* 一个Request拆分出的异步AioRequest的数量 */
    env_atomic curr_;
    int error_;
};

class AioRequest {
public:
    AioRequest(Segment *segment, std::shared_ptr<RequestCounter> counter)
        : segment_(segment), counter_(counter)
    {}

    virtual bool Complete(int64_t res)
	{
		if (res != segment_->length) {
            counter_->error_ = AIO_REQUEST_ERR;
        }
        return counter_->AddAndComplete();
	}
    virtual ~AioRequest() {}

public:
    Segment *segment_;
    std::shared_ptr<RequestCounter> counter_;
};

extern "C" int InitChunkPool(const char *disk_path)
{
	int fd = open(disk_path, O_RDWR | O_DIRECT, 0660);
	if (fd < 0) {
		ocf_adaptor_log(OCF_LOG_ERROR, "open disk(%s) fail\n", disk_path);
		return -1;
	}
	g_disk_fd = fd;

	uint64_t pool_size;
	int ret = ioctl(g_disk_fd, BLKGETSIZE64, &pool_size);
	if (ret) {
		ocf_adaptor_log(OCF_LOG_ERROR, "get pool size fail, errno %d\n", errno);
		return -1;
	}
	g_chunk_num = pool_size / CHUNK_SIZE;

	memset(&g_ctx, 0, sizeof(g_ctx));
	ret = io_setup(AIO_DEPTH, &g_ctx);
	if (ret) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_setup fail, errno %d\n", errno);
		return -1;
	}

	g_chunks = new Chunk[g_chunk_num];
	if (unlikely(!g_chunks)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "chunk create fail\n");
		io_destroy(g_ctx);
		return -1;
	}

	for (uint64_t i = 0; i < g_chunk_num; ++i) {
		g_chunks[i].chunk_id = i;
		g_chunks[i].offset = i * CHUNK_SIZE;
		g_chunks[i].size = CHUNK_SIZE;
		g_chunks[i].allocated = false;
	}
	pthread_mutex_init(&g_chunk_mutex, nullptr);
	return 0;
}

int AllocChunks(std::size_t num, std::vector<uint64_t> *chunk_ids)
{
	std::size_t cnt = 0;

	pthread_mutex_lock(&g_chunk_mutex);
	for (uint64_t i = 0; i < g_chunk_num && cnt < num; ++i) {
		if (!g_chunks[i].allocated) {
			g_chunks[i].allocated = true;
			chunk_ids->push_back(i);
			++cnt;
		}
	}
	pthread_mutex_unlock(&g_chunk_mutex);

	if (cnt < num) {
		ocf_adaptor_log(OCF_LOG_ERROR, "alloc chunk fail\n");
		FreeChunks(chunk_ids);
		chunk_ids->clear();
		return -1;
	}

	return 0;
}

int FreeChunks(std::vector<uint64_t> *chunk_ids)
{
	pthread_mutex_lock(&g_chunk_mutex);
	for (uint64_t id : *chunk_ids) {
		g_chunks[id].allocated = false;
	}
	pthread_mutex_unlock(&g_chunk_mutex);
	return 0;
}

int SubmitWrite(uint64_t chunk_id, Segment *seg, std::shared_ptr<RequestCounter> counter)
{
	int ret;
	struct iocb iocbpp;
	struct iocb *p = &iocbpp;
	AioRequest *req = new AioRequest(seg, counter);
	uint64_t offset = chunk_id * CHUNK_SIZE + seg->offset;
	io_prep_pwrite(p, g_disk_fd, seg->data, seg->length, offset);
	iocbpp.data = req;
	do {
		ret = io_submit(g_ctx, 1, &p);
		if (ret == -EAGAIN) {
			PollCompletion(128);
		}
	} while (ret == -EAGAIN);

	return 0;
}

int SubmitRead(uint64_t chunk_id, Segment *seg, std::shared_ptr<RequestCounter> counter)
{
	int ret;
	struct iocb iocbpp;
	struct iocb *p = &iocbpp;
	AioRequest *req = new AioRequest(seg, counter);
	uint64_t offset = chunk_id * CHUNK_SIZE + seg->offset;
	io_prep_pread(p, g_disk_fd, seg->data, seg->length, offset);
	iocbpp.data = req;
	do {
		ret = io_submit(g_ctx, 1, &p);
		if (ret == -EAGAIN) {
			PollCompletion(128);
		}
	} while (ret == -EAGAIN);

	return 0;
}

int AioWrite(Request_t req)
{
	SpinSleepUS(NETWORK_DELAY_US);		/* sleep for network delay mock */

	int size = req->segments.size();
	std::shared_ptr<RequestCounter> counter = std::make_shared<RequestCounter>(req, size);
	for (int i = 0; i < size; ++i) {
		SubmitWrite(req->chunk_id, &req->segments[i], counter);
	}
	return 0;
}

int AioRead(Request_t req)
{
	SpinSleepUS(NETWORK_DELAY_US);		/* sleep for network delay mock */

	int size = req->segments.size();
	std::shared_ptr<RequestCounter> counter = std::make_shared<RequestCounter>(req, size);
	for (int i = 0; i < size; ++i) {
		SubmitRead(req->chunk_id, &req->segments[i], counter);
	}
	return 0;
}

int PollCompletion(uint32_t max)
{
	int request_complete_cnt = 0; /* 记录上层下发的Request完成数量，Request里的所有segment都处理完毕 */
	struct io_event *events = new io_event[max];
	int num_events = io_getevents(g_ctx, 0, max, events, nullptr);
	for (int i = 0; i < num_events; ++i) {
		struct io_event *event = &events[i];
		AioRequest *req = static_cast<AioRequest *>(event->data);
		if (req->Complete(event->res)) {
			request_complete_cnt++;
			SpinSleepUS(NETWORK_DELAY_US);	/* sleep for network delay mock */
			req->counter_->Callback();
		}
		delete req;
	}
	delete[] events;
	return request_complete_cnt;
}