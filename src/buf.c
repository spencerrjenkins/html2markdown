/*
 * Growable byte buffer and checked allocation helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#include "buf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void h2m_oom(void)
{
    fputs("html2md: out of memory\n", stderr);
    abort();
}

void *h2m_xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        h2m_oom();
    return p;
}

void *h2m_xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p)
        h2m_oom();
    return p;
}

void *h2m_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        h2m_oom();
    return q;
}

char *h2m_xstrndup(const char *s, size_t n)
{
    char *p = h2m_xmalloc(n + 1);
    if (n)
        memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *h2m_xstrdup(const char *s)
{
    return h2m_xstrndup(s, strlen(s));
}

void h2m_buf_init(h2m_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void h2m_buf_free(h2m_buf *b)
{
    free(b->data);
    h2m_buf_init(b);
}

void h2m_buf_clear(h2m_buf *b)
{
    b->len = 0;
    if (b->data)
        b->data[0] = '\0';
}

void h2m_buf_reserve(h2m_buf *b, size_t extra)
{
    if (extra > SIZE_MAX - b->len - 1)
        h2m_oom();
    size_t need = b->len + extra + 1;
    if (need <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    b->data = h2m_xrealloc(b->data, cap);
    b->cap = cap;
}

void h2m_buf_append(h2m_buf *b, const char *s, size_t n)
{
    if (n == 0) {
        if (!b->data) {
            h2m_buf_reserve(b, 0);
            b->data[0] = '\0';
        }
        return;
    }
    h2m_buf_reserve(b, n);
    memmove(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void h2m_buf_puts(h2m_buf *b, const char *s)
{
    h2m_buf_append(b, s, strlen(s));
}

void h2m_buf_putc(h2m_buf *b, char c)
{
    h2m_buf_reserve(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void h2m_buf_repeat(h2m_buf *b, char c, size_t n)
{
    if (n == 0)
        return;
    h2m_buf_reserve(b, n);
    memset(b->data + b->len, c, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void h2m_buf_truncate(h2m_buf *b, size_t len)
{
    if (len < b->len) {
        b->len = len;
        b->data[len] = '\0';
    }
}

void h2m_buf_splice(h2m_buf *b, size_t pos, size_t del, const char *s, size_t n)
{
    if (pos > b->len)
        pos = b->len;
    if (del > b->len - pos)
        del = b->len - pos;
    if (n > del)
        h2m_buf_reserve(b, n - del);
    if (!b->data) {
        h2m_buf_reserve(b, 0);
        b->data[0] = '\0';
    }
    memmove(b->data + pos + n, b->data + pos + del, b->len - pos - del);
    if (n)
        memcpy(b->data + pos, s, n);
    b->len = b->len - del + n;
    b->data[b->len] = '\0';
}

void h2m_buf_printf(h2m_buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        h2m_buf_reserve(b, (size_t)n);
        vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

const char *h2m_buf_cstr(h2m_buf *b)
{
    if (!b->data)
        return "";
    return b->data;
}

char *h2m_buf_detach(h2m_buf *b, size_t *len)
{
    if (!b->data) {
        h2m_buf_reserve(b, 0);
        b->data[0] = '\0';
    }
    char *p = b->data;
    if (len)
        *len = b->len;
    h2m_buf_init(b);
    return p;
}

char h2m_buf_last(const h2m_buf *b)
{
    return b->len ? b->data[b->len - 1] : '\0';
}

void h2m_buf_rtrim_spaces(h2m_buf *b)
{
    while (b->len && (b->data[b->len - 1] == ' ' || b->data[b->len - 1] == '\t'))
        b->len--;
    if (b->data)
        b->data[b->len] = '\0';
}
