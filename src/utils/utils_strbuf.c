/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: string buffer
 * Create: 2024-02-02
 */
#include <strings.h>

#include "utils_strbuf.h"
#include "utils_realloc.h"
#include "ocf_env.h"

struct strbuf* new_strbuf()
{
    struct strbuf *ret;
    ret = (struct strbuf*)env_zalloc(sizeof(*ret), ENV_MEM_NOIO);
    if(!ret) {
        return ret;
    }

    OCF_REALLOC_INIT(&(ret->buf), &(ret->len));
	if(OCF_REALLOC(&(ret->buf), DEFAULT_BUF_SIZE, 1, &(ret->len))) {
		return NULL;
	}

    OCF_REALLOC_INIT(&(ret->format_buf), &(ret->format_buf_len));
	if(OCF_REALLOC(&(ret->format_buf), DEFAULT_PRINT_BUF_SIZE, 1, &(ret->format_buf_len))) {
		return NULL;
	}
    return ret;
}

void delete_strbuf(struct strbuf *b)
{
    OCF_REALLOC_DEINIT(&(b->buf), &(b->len));
    OCF_REALLOC_DEINIT(&(b->format_buf), &(b->format_buf_len));
    env_free(b);
}

int strbuf_write_format_str(struct strbuf *b, const char *format, ...)
{
    va_list args;
    int count;

    va_start (args, format);
    count = vsnprintf(b->format_buf, b->format_buf_len, format, args);
    if(count < 0) {
        return -1;
    }
    if(count >= b->format_buf_len) {
        /* need expand format buf space */
        if(OCF_REALLOC_CP(&(b->format_buf), b->format_buf_len+count*2, 1, &(b->format_buf_len))) {
            return -1;
        }
        /* retry, just check negative return value */
        count = vsnprintf(b->format_buf, b->format_buf_len, format, args);
        if(count < 0) {
            return -1;
        }
    }
    va_end(args);

    return strbuf_write_str(b, b->format_buf);
}

int strbuf_write_str(struct strbuf *b, const char *str)
{
    size_t needspace = strlen(str);
    if(needspace > (b->len-b->cur)) {
        /* need expand space */
        if(OCF_REALLOC_CP(&(b->buf), b->len+needspace*2, 1, &(b->len))) {
            return -1;
        }
    }

    strncpy(b->buf+b->cur, str, min(b->len-b->cur, needspace));
    b->cur += needspace;
    return needspace;
}

int strbuf_write_char(struct strbuf *b, char c)
{
    if(1 > (b->len-b->cur)) {
        /* need expand space */
        if(OCF_REALLOC_CP(&(b->buf), b->len+2, 1, &(b->len))) {
            return -1;
        }
    }
    *(b->buf+b->cur) = c;
    b->cur++;
    return 1;
}

int strbuf_write_end(struct strbuf *b)
{
    return strbuf_write_char(b, '\0');
}
