/*
 * Copyright(c) 2012-2021 Intel Corporation
 * Copyright(c) 2024 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <ocf/ocf.h>
#include <stdio.h>
#include "log.h"
#include "volume.h"
#include "ctx.h"

#define MAX_BUF 1024

static int ctx_logger_print(ocf_logger_t logger, ocf_logger_lvl_t lvl, const char *fmt, va_list args)
{
	int ocf_log_lvl = lvl;
	char buf[MAX_BUF];
	log_print_func print = get_log_print();

	int ret = vsnprintf(buf, sizeof(buf), fmt, args);
	if (ret < 0) {
		print(OCF_LOG_ERROR, "the print is too long\n");
		return -1;
	}

	return print((ocf_log_level)ocf_log_lvl, buf);
}

static const struct ocf_ctx_config ctx_cfg = {
	.name = "lava adaptor",
	.ops = {
		.data = {
		},

		.cleaner = {
		},

		.logger = {
			.print = ctx_logger_print,
		},
	},
};

int ctx_init(ocf_ctx_t *ctx)
{
	int32_t ret = ocf_ctx_create(ctx, &ctx_cfg);
	if (ret) {
		return ret;
	}

	ret = volume_init(*ctx);
	if (ret) {
		ocf_ctx_put(*ctx);
		return ret;
	}
	return 0;
}

void ctx_cleanup(ocf_ctx_t ctx)
{
	volume_cleanup(ctx);
	ocf_ctx_put(ctx);
}