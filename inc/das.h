#ifndef __DAS_H__
#define __DAS_H__

#ifdef __cplusplus
extern "C"
{
#endif

enum DAS_RESULT {
    RETURN_DAS_FULL = -2,
    RETURN_DAS_ERROR = -1,
    RETURN_DAS_OK = 0,
    RETURN_DAS_EMPTY = 1,
    RETURN_DAS_DELETING = 2,
    RETURN_DAS_REJECT = 3,
};

enum DasAlgType
    DAS_ALG_SEQ = 0,
    DAS_ALG_REVERSE_SEQ,
    DAS_ALG_STRIDE,
    DAS_ALG_BUTT,
};

enum DasLogLvl {
    DAS_LOGLVL_ERR = 0,
    DAS_LOGLVL_WAR,
    DAS_LOGLVL_INF,
    DAS_LOGLVL_DBG,
};

/* key string */
typedef struct DasKeyStr {
    uint32_t len;
    char *buf;
} DasKeyStr;

typedef struct DasKvParam {
    uint64_t offset;
    uint64_t len;
    uint64_t timeStamp;
    void *volume;
    void *queue;
} DasKvParam;

typedef struct DasOPS {
    int (*SubmitDasPrefetch)(struct DasKvParam* params);
    void (*logFunc)(void *logger, enum DasLogLvl level, const char *format, ...);
} DasOPS;

/* Param to create DAS */
typedef struct DasModuleParam {
    struct DasOPS *ops;
    void *logger;
    uint64_t cacheLineSize;
} DasModuleParam;

int32_t Rcache_CeateDasModule(DasModuleParam *createInstanceParam);

void Rcache_ExitDasModule();

void ConsumerTask(DasModuleParam *createInstanceParam);

int32_t Rcache_PutDasInfo(DasKvParam *params);

#ifdef __cplusplus
}
#endif

#endif /* __DAS_H__ */