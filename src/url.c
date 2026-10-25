/*
 * URL helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#include "url.h"

#include <string.h>

void h2m_url_clean(const char *url, h2m_buf *out)
{
    size_t len = strlen(url);
    size_t start = 0;
    while (start < len && (unsigned char)url[start] <= 0x20)
        start++;
    while (len > start && (unsigned char)url[len - 1] <= 0x20)
        len--;
    for (size_t i = start; i < len; i++) {
        char c = url[i];
        if (c == '\t' || c == '\n' || c == '\r')
            continue;
        h2m_buf_putc(out, c);
    }
    if (!out->data)
        h2m_buf_append(out, "", 0);
}

static bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static size_t scheme_len(const char *url)
{
    if (!is_alpha(url[0]))
        return 0;
    size_t i = 1;
    while (is_alpha(url[i]) || (url[i] >= '0' && url[i] <= '9') || url[i] == '+' || url[i] == '-' ||
           url[i] == '.')
        i++;
    return url[i] == ':' ? i : 0;
}

bool h2m_url_has_scheme(const char *url)
{
    return scheme_len(url) > 0;
}

bool h2m_url_scheme_is(const char *url, const char *scheme)
{
    size_t n = scheme_len(url);
    if (n == 0 || n != strlen(scheme))
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = url[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != scheme[i])
            return false;
    }
    return true;
}

/* Components of a URI reference as slices into the source string. */
typedef struct {
    const char *scheme;
    size_t scheme_len;
    const char *authority; /* NULL when absent */
    size_t authority_len;
    const char *path;
    size_t path_len;
    const char *query; /* NULL when absent; excludes '?' */
    size_t query_len;
    const char *fragment; /* NULL when absent; excludes '#' */
    size_t fragment_len;
} uri_parts;

static void split_uri(const char *s, uri_parts *u)
{
    memset(u, 0, sizeof *u);
    size_t sl = scheme_len(s);
    if (sl) {
        u->scheme = s;
        u->scheme_len = sl;
        s += sl + 1;
    }
    if (s[0] == '/' && s[1] == '/') {
        s += 2;
        size_t n = strcspn(s, "/?#");
        u->authority = s;
        u->authority_len = n;
        s += n;
    }
    size_t n = strcspn(s, "?#");
    u->path = s;
    u->path_len = n;
    s += n;
    if (*s == '?') {
        s++;
        n = strcspn(s, "#");
        u->query = s;
        u->query_len = n;
        s += n;
    }
    if (*s == '#') {
        s++;
        u->fragment = s;
        u->fragment_len = strlen(s);
    }
}

/* RFC 3986 section 5.2.4. */
static void remove_dot_segments(const char *in, size_t len, h2m_buf *out)
{
    h2m_buf work = H2M_BUF_INIT;
    size_t i = 0;
    while (i < len) {
        const char *p = in + i;
        size_t rest = len - i;
        if (rest >= 3 && memcmp(p, "../", 3) == 0) {
            i += 3;
        } else if (rest >= 2 && memcmp(p, "./", 2) == 0) {
            i += 2;
        } else if (rest >= 3 && memcmp(p, "/./", 3) == 0) {
            i += 2;
        } else if (rest == 2 && memcmp(p, "/.", 2) == 0) {
            /* replace "/." with "/" */
            h2m_buf_putc(&work, '/');
            i += 2;
        } else if ((rest >= 4 && memcmp(p, "/../", 4) == 0) || (rest == 3 && memcmp(p, "/..", 3) == 0)) {
            bool final = rest == 3;
            i += 3;
            /* remove the last segment from the output */
            while (work.len && work.data[work.len - 1] != '/')
                work.len--;
            if (work.len)
                work.len--;
            if (work.data)
                work.data[work.len] = '\0';
            if (final)
                h2m_buf_putc(&work, '/');
        } else if ((rest == 1 && p[0] == '.') || (rest == 2 && memcmp(p, "..", 2) == 0)) {
            i = len;
        } else {
            size_t j = (p[0] == '/') ? 1 : 0;
            while (j < rest && p[j] != '/')
                j++;
            h2m_buf_append(&work, p, j);
            i += j;
        }
    }
    h2m_buf_append(out, work.data ? work.data : "", work.len);
    h2m_buf_free(&work);
}

void h2m_url_resolve(const char *base, const char *ref, h2m_buf *out)
{
    if (!base || !h2m_url_has_scheme(base) || !ref) {
        h2m_buf_puts(out, ref ? ref : "");
        return;
    }
    uri_parts b, r;
    split_uri(base, &b);
    split_uri(ref, &r);

    /* Fragment-only and empty references are kept as-is: they point into
     * the converted document itself. */
    if (!r.scheme && !r.authority && r.path_len == 0 && !r.query) {
        h2m_buf_puts(out, ref);
        return;
    }

    h2m_buf path = H2M_BUF_INIT;
    const char *scheme, *auth, *query;
    size_t scheme_n, auth_n, query_n;
    bool has_auth, has_query;

    if (r.scheme) {
        scheme = r.scheme;
        scheme_n = r.scheme_len;
        has_auth = r.authority != NULL;
        auth = r.authority;
        auth_n = r.authority_len;
        remove_dot_segments(r.path, r.path_len, &path);
        has_query = r.query != NULL;
        query = r.query;
        query_n = r.query_len;
    } else {
        scheme = b.scheme;
        scheme_n = b.scheme_len;
        if (r.authority) {
            has_auth = true;
            auth = r.authority;
            auth_n = r.authority_len;
            remove_dot_segments(r.path, r.path_len, &path);
            has_query = r.query != NULL;
            query = r.query;
            query_n = r.query_len;
        } else {
            has_auth = b.authority != NULL;
            auth = b.authority;
            auth_n = b.authority_len;
            if (r.path_len == 0) {
                h2m_buf_append(&path, b.path, b.path_len);
                has_query = r.query != NULL || b.query != NULL;
                query = r.query ? r.query : b.query;
                query_n = r.query ? r.query_len : b.query_len;
            } else {
                h2m_buf merged = H2M_BUF_INIT;
                if (r.path[0] == '/') {
                    h2m_buf_append(&merged, r.path, r.path_len);
                } else {
                    /* RFC 3986 section 5.2.3 merge */
                    if (b.authority && b.path_len == 0) {
                        h2m_buf_putc(&merged, '/');
                    } else {
                        size_t keep = b.path_len;
                        while (keep && b.path[keep - 1] != '/')
                            keep--;
                        h2m_buf_append(&merged, b.path, keep);
                    }
                    h2m_buf_append(&merged, r.path, r.path_len);
                }
                remove_dot_segments(merged.data ? merged.data : "", merged.len, &path);
                h2m_buf_free(&merged);
                has_query = r.query != NULL;
                query = r.query;
                query_n = r.query_len;
            }
        }
    }

    h2m_buf_append(out, scheme, scheme_n);
    h2m_buf_putc(out, ':');
    if (has_auth) {
        h2m_buf_puts(out, "//");
        h2m_buf_append(out, auth, auth_n);
    }
    if (path.len)
        h2m_buf_append(out, path.data, path.len);
    if (has_query) {
        h2m_buf_putc(out, '?');
        h2m_buf_append(out, query, query_n);
    }
    if (r.fragment) {
        h2m_buf_putc(out, '#');
        h2m_buf_append(out, r.fragment, r.fragment_len);
    }
    h2m_buf_free(&path);
}
