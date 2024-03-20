/*
 * Copyright(c) 2022 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __UTILS_GENERATOR_H__
#define __UTILS_GENERATOR_H__

#include "ocf/ocf.h"

struct ocf_generator_bisect_state {
	ocf_cache_line_t curr;
	ocf_cache_line_t limit;
};

void ocf_generator_bisect_init(
		struct ocf_generator_bisect_state *generator,
		ocf_cache_line_t limit, ocf_cache_line_t offset);

ocf_cache_line_t ocf_generator_bisect_next(
		struct ocf_generator_bisect_state *generator);

#endif /* __UTILS_GENERATOR_H__ */
