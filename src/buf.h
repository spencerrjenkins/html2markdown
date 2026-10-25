/*
 * Growable byte buffer. The contents are always NUL-terminated once any
 * data has been appended, so `data` can be used as a C string.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_BUF_H
#define H2M_BUF_H

#include <stdarg.h>
#include <stddef.h>

typedef struct h2m_buf {
    char *data;
    size_t len;
    size_t cap;
} h2m_buf;

#define H2M_BUF_INIT {NULL, 0, 0}

void h2m_buf_init(h2m_buf *b);
void h2m_buf_free(h2m_buf *b);
void h2m_buf_clear(h2m_buf *b);
void h2m_buf_reserve(h2m_buf *b, size_t extra);
void h2m_buf_append(h2m_buf *b, const char *s, size_t n);
void h2m_buf_puts(h2m_buf *b, const char *s);
void h2m_buf_putc(h2m_buf *b, char c);
void h2m_buf_repeat(h2m_buf *b, char c, size_t n);
void h2m_buf_truncate(h2m_buf *b, size_t len);
void h2m_buf_splice(h2m_buf *b, size_t pos, size_t del, const char *s, size_t n);
void h2m_buf_printf(h2m_buf *b, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Returns a NUL-terminated string (never NULL, "" when empty). */
const char *h2m_buf_cstr(h2m_buf *b);

/* Transfers ownership of the storage to the caller and resets the buffer. */
char *h2m_buf_detach(h2m_buf *b, size_t *len);

/* Returns the last byte, or 0 when empty. */
char h2m_buf_last(const h2m_buf *b);

/* Removes trailing spaces and tabs. */
void h2m_buf_rtrim_spaces(h2m_buf *b);

/* Aborts the process after printing a diagnostic (allocation failure). */
void h2m_oom(void)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noreturn))
#endif
    ;

void *h2m_xmalloc(size_t n);
void *h2m_xcalloc(size_t count, size_t size);
void *h2m_xrealloc(void *p, size_t n);
char *h2m_xstrdup(const char *s);
char *h2m_xstrndup(const char *s, size_t n);

#endif /* H2M_BUF_H */
