#ifndef __OCF_DAS_H__
#define __OCF_DAS_H__

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "ocf/ocf_def.h"
#include "ocf/ocf_types.h"
#include "ocf/ocf_io.h"

#define DAS_INITED 1

bool das_get_status(void);
void das_init(ocf_cache_t cache);
void das_exit(void);
void init_das_limiter(ocf_cache_t cache, ocf_core_t core);
void set_das_limiter(ocf_core_t core, uint32_t capacity, uint32_t leak_rate);
void das_analyze_io(struct ocf_io *io);

#endif /* __OCF_DAS_H__ */