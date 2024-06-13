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
#include <queue>
#include "chunk.h"
#include "log.h"
#include "ocf_env.h"
#include "ocf_adaptor_err.h"

using namespace std;

#define AIO_REQUEST_ERR 1
#define CHUNK_SIZE (1 << 27)	// 128MiB
#define NETWORK_DELAY_US 10
#define SEC2USEC 1000000
#define IO_SUBMIT_POLL 5
#define QUEUE_NUM_LIMIT 4096
#define SUBMIT_NUM_LIMIT 128 // 1M / cache_line_size

#define AIO_DEPTH 0x80000

typedef struct {
	uint64_t chunk_id;
	uint64_t offset;		/* chunk在磁盘上的起始位置 */
	uint32_t size;
	bool allocated;			/* chunk是否已分配 */
} Chunk;					/* chunk对接磁盘 */
typedef Chunk *Chunk_t;

typedef struct {
    int op;
    Request_t req;
} OpCtx;
typedef OpCtx *OpCtx_t;

class RequestCounter {
public:
	RequestCounter(Request_t req, int max) : req_(req), max_(max), error_(0)
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
	Request_t req_; /* 原始的Request */
	int max_;      /* 一个Request拆分出的异步AioRequest的数量 */
	env_atomic curr_;
	int error_;
};
typedef RequestCounter *RequestCounter_t;

class AioRequest {
public:
	AioRequest(Segment_t segment, RequestCounter_t counter)
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
	Segment_t segment_;
	RequestCounter_t counter_;
};
typedef AioRequest *AioRequest_t;

static int g_disk_fd;
static uint64_t g_chunk_num;			/* chunk总数 */
static Chunk_t g_chunks = nullptr;
static pthread_mutex_t g_chunk_mutex;	/* 申请chunk时加锁 */
static io_context_t g_ctx;
static pthread_t t1;
static pthread_t t2;
static int stop;
static pthread_spinlock_t sq_lock;
static pthread_spinlock_t cq_lock;
static struct iocb *ic[SUBMIT_NUM_LIMIT];
static struct iocb ics[SUBMIT_NUM_LIMIT];
static struct io_event ie[QUEUE_NUM_LIMIT];
static OpCtx sv[QUEUE_NUM_LIMIT];
static queue<OpCtx> sq;
static queue<RequestCounter_t> cq;

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

static void* aio_process(void *arg);

static void* aio_poll(void *arg);

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
		close(fd);
		return -1;
	}
	g_chunk_num = pool_size / CHUNK_SIZE;
	ocf_adaptor_log(OCF_LOG_INFO, "chunk max num %lu\n", g_chunk_num);

	memset(&g_ctx, 0, sizeof(g_ctx));
	ret = io_setup(AIO_DEPTH, &g_ctx);
	if (ret) {
		ocf_adaptor_log(OCF_LOG_ERROR, "io_setup fail, errno %d\n", errno);
		close(fd);
		return -1;
	}

	for (int i = 0; i < SUBMIT_NUM_LIMIT; ++i) {
		ic[i] = &ics[i];
	}

	g_chunks = new Chunk[g_chunk_num];
	if (unlikely(!g_chunks)) {
		ocf_adaptor_log(OCF_LOG_ERROR, "chunk create fail\n");
		io_destroy(g_ctx);
		close(fd);
		return -1;
	}

	for (uint64_t i = 0; i < g_chunk_num; ++i) {
		g_chunks[i].chunk_id = i;
		g_chunks[i].offset = i * CHUNK_SIZE;
		g_chunks[i].size = CHUNK_SIZE;
		g_chunks[i].allocated = false;
	}
	pthread_mutex_init(&g_chunk_mutex, nullptr);
	pthread_spin_init(&sq_lock, 0);
	pthread_spin_init(&cq_lock, 0);
	stop = false;
	pthread_create(&t1, NULL, aio_process, nullptr);
	pthread_create(&t2, NULL, aio_poll, nullptr);
	return 0;
}

extern "C" void DeInitChunkPool()
{
	if (g_chunks) {
		stop = true;
		pthread_join(t1, NULL);
		pthread_join(t2, NULL);
		pthread_spin_destroy(&sq_lock);
		pthread_spin_destroy(&cq_lock);
		delete[] g_chunks;
		g_chunks = nullptr;
		pthread_mutex_destroy(&g_chunk_mutex);
		io_destroy(g_ctx);
		close(g_disk_fd);
	}
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

static int Push(int op, Request_t req)
{
	pthread_spin_lock(&sq_lock);
	if (sq.size() >= QUEUE_NUM_LIMIT) {
		pthread_spin_unlock(&sq_lock);
		return -1;
	}
	sq.push({op, req});
	pthread_spin_unlock(&sq_lock);
	return 0;
}

static int Submit(int op, Request_t req)
{
	int ret;
	struct iocb **ic_now;

	int size = req->segments.size();
	uint64_t chunk_id = req->chunk_id;
	RequestCounter_t counter = new RequestCounter(req, size);

	if (op == 0) {
		for (int i = 0; i < size; ++i) {
			Segment *seg = &req->segments[i];
			AioRequest_t a = new AioRequest(seg, counter);
			uint64_t offset = chunk_id * CHUNK_SIZE + seg->offset;
			io_prep_pread(ic[i], g_disk_fd, seg->data, seg->length, offset);
			ic[i]->data = a;
		}
	} else {
		for (int i = 0; i < size; ++i) {
			Segment *seg = &req->segments[i];
			AioRequest_t a = new AioRequest(seg, counter);
			uint64_t offset = chunk_id * CHUNK_SIZE + seg->offset;
			io_prep_pwrite(ic[i], g_disk_fd, seg->data, seg->length, offset);
			ic[i]->data = a;
		}
	}

	ic_now = ic;
	while (size > 0) {
		ret = io_submit(g_ctx, size, ic_now);
		if (ret >= 0) {
			size -= ret;
			ic_now += ret;
		} else if (ret == -EAGAIN) {
			ocf_adaptor_log(OCF_LOG_ERROR, "io_submit again\n");
		} else {
			ocf_adaptor_log(OCF_LOG_ERROR, "io_submit fail, ret %d\n", ret);
		}
	}

	return 0;
}

int AioWrite(Request_t req)
{
	if (unlikely(!g_chunks[req->chunk_id].allocated)) {
		return STATE_CHUNK_UNAVAILABLE;
	}

	SpinSleepUS(NETWORK_DELAY_US);		/* sleep for network delay mock */
	return Push(1, req);
}

int AioRead(Request_t req)
{
	if (unlikely(!g_chunks[req->chunk_id].allocated)) {
		return STATE_CHUNK_UNAVAILABLE;
	}

	SpinSleepUS(NETWORK_DELAY_US);		/* sleep for network delay mock */
	return Push(0, req);
}

int PollChunkCompletion(uint32_t max_nr)
{
	int n;
	RequestCounter_t *cv = new RequestCounter_t[max_nr];
	if (unlikely(cv == nullptr)) {
		return 0;
	}
	pthread_spin_lock(&cq_lock);
	n = (max_nr < cq.size() ? max_nr : cq.size());
	for (int i = 0; i < n; ++i) {
		cv[i] = cq.front();
		cq.pop();
	}
	pthread_spin_unlock(&cq_lock);

	for (int i = 0; i < n; ++i) {
		RequestCounter_t counter = cv[i];
		counter->Callback();
		delete counter;
	}
	delete []cv;
	return n;
}

static void* aio_process(void *arg)
{
	int n;
	while (!(stop)) {
		pthread_spin_lock(&sq_lock);
		n = ((QUEUE_NUM_LIMIT < sq.size()) ? QUEUE_NUM_LIMIT : sq.size());
		for (int i = 0; i < n; ++i) {
			sv[i] = sq.front();
			sq.pop();
		}
		pthread_spin_unlock(&sq_lock);

		for (int i = 0; i < n; ++i) {
			Submit(sv[i].op, sv[i].req);
		}

		if (n == 0) {
			usleep(10);
		}
	}

	pthread_exit(0);
}

static void* aio_poll(void *arg)
{
	int n;
	while (!(stop)) {
		n = io_getevents(g_ctx, 0, QUEUE_NUM_LIMIT, ie, nullptr);
		for (int i = 0; i < n; ++i) {
			struct io_event *event = &ie[i];
			AioRequest_t req = static_cast<AioRequest_t>(event->data);
			if (req->Complete(event->res)) {
				pthread_spin_lock(&cq_lock);
				cq.push(req->counter_);
				pthread_spin_unlock(&cq_lock);
			}
			delete req;
		}

		if (n == 0) {
			usleep(10);
		}
	}

	pthread_exit(0);
}