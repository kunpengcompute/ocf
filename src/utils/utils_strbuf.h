/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: string buffer
 * Create: 2024-02-02
 */

#ifndef __UTILS_STRBUF_H__
#define __UTILS_STRBUF_H__

#include <stdbool.h>

#define DEFAULT_BUF_SIZE 4096
#define DEFAULT_PRINT_BUF_SIZE 128

#ifdef __cplusplus
extern "C" {
#endif

struct strbuf
{
	size_t len;
	size_t cur;
	size_t format_buf_len;
	char *buf;
	char *format_buf;
};

struct strbuf* new_strbuf();
void delete_strbuf(struct strbuf *b);
int strbuf_write_format_str(struct strbuf *b, const char *format, ...);
int strbuf_write_str(struct strbuf *b, const char *str);
int strbuf_write_char(struct strbuf *b, char c);
int strbuf_write_end(struct strbuf *b);
bool using_scientific_notation(double num);

#ifdef __cplusplus
}
#endif

#endif /* __UTILS_STRBUF_H__ */