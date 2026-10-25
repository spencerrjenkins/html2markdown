/*
 * URL helpers: cleanup of attribute URLs and RFC 3986 reference resolution.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_URL_H
#define H2M_URL_H

#include <stdbool.h>
#include <stddef.h>

#include "buf.h"

/*
 * Appends `url` to out after the WHATWG URL parser's input cleanup: leading
 * and trailing C0-control/space characters are trimmed and ASCII tab/newline
 * characters are removed.
 */
void h2m_url_clean(const char *url, h2m_buf *out);

/* True when the string starts with a URI scheme followed by ':'. */
bool h2m_url_has_scheme(const char *url);

/* True when the scheme matches `scheme` (ASCII case-insensitive). */
bool h2m_url_scheme_is(const char *url, const char *scheme);

/*
 * Resolves `ref` against the absolute URL `base` (RFC 3986 section 5.2) and
 * appends the result to out. When base is NULL or not absolute, `ref` is
 * appended unchanged.
 */
void h2m_url_resolve(const char *base, const char *ref, h2m_buf *out);

#endif /* H2M_URL_H */
