/*
 * URL resolution tests, including the RFC 3986 section 5.4 examples.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "testlib.h"
#include "url.h"

static char *resolve(const char *base, const char *ref)
{
    h2m_buf b = H2M_BUF_INIT;
    h2m_url_resolve(base, ref, &b);
    return h2m_buf_detach(&b, NULL);
}

#define CHECK_RESOLVE(base, ref, expected)   \
    do {                                     \
        char *r_ = resolve(base, ref);       \
        CHECK_EQ_STR(expected, r_);          \
        free(r_);                            \
    } while (0)

void suite_url(void)
{
    const char *b = "http://a/b/c/d;p?q";
    /* RFC 3986 5.4.1 normal examples */
    CHECK_RESOLVE(b, "g:h", "g:h");
    CHECK_RESOLVE(b, "g", "http://a/b/c/g");
    CHECK_RESOLVE(b, "./g", "http://a/b/c/g");
    CHECK_RESOLVE(b, "g/", "http://a/b/c/g/");
    CHECK_RESOLVE(b, "/g", "http://a/g");
    CHECK_RESOLVE(b, "//g", "http://g");
    CHECK_RESOLVE(b, "?y", "http://a/b/c/d;p?y");
    CHECK_RESOLVE(b, "g?y", "http://a/b/c/g?y");
    CHECK_RESOLVE(b, "#s", "#s"); /* kept relative: points into this document */
    CHECK_RESOLVE(b, "g#s", "http://a/b/c/g#s");
    CHECK_RESOLVE(b, "g?y#s", "http://a/b/c/g?y#s");
    CHECK_RESOLVE(b, ";x", "http://a/b/c/;x");
    CHECK_RESOLVE(b, "g;x", "http://a/b/c/g;x");
    CHECK_RESOLVE(b, "g;x?y#s", "http://a/b/c/g;x?y#s");
    CHECK_RESOLVE(b, ".", "http://a/b/c/");
    CHECK_RESOLVE(b, "./", "http://a/b/c/");
    CHECK_RESOLVE(b, "..", "http://a/b/");
    CHECK_RESOLVE(b, "../", "http://a/b/");
    CHECK_RESOLVE(b, "../g", "http://a/b/g");
    CHECK_RESOLVE(b, "../..", "http://a/");
    CHECK_RESOLVE(b, "../../", "http://a/");
    CHECK_RESOLVE(b, "../../g", "http://a/g");
    /* 5.4.2 abnormal examples */
    CHECK_RESOLVE(b, "../../../g", "http://a/g");
    CHECK_RESOLVE(b, "../../../../g", "http://a/g");
    CHECK_RESOLVE(b, "/./g", "http://a/g");
    CHECK_RESOLVE(b, "/../g", "http://a/g");
    CHECK_RESOLVE(b, "g.", "http://a/b/c/g.");
    CHECK_RESOLVE(b, ".g", "http://a/b/c/.g");
    CHECK_RESOLVE(b, "g..", "http://a/b/c/g..");
    CHECK_RESOLVE(b, "..g", "http://a/b/c/..g");
    CHECK_RESOLVE(b, "./../g", "http://a/b/g");
    CHECK_RESOLVE(b, "./g/.", "http://a/b/c/g/");
    CHECK_RESOLVE(b, "g/./h", "http://a/b/c/g/h");
    CHECK_RESOLVE(b, "g/../h", "http://a/b/c/h");
    CHECK_RESOLVE(b, "g;x=1/./y", "http://a/b/c/g;x=1/y");
    CHECK_RESOLVE(b, "g;x=1/../y", "http://a/b/c/y");
    CHECK_RESOLVE(b, "http:g", "http:g");

    CHECK_RESOLVE("https://example.com", "page", "https://example.com/page");
    CHECK_RESOLVE("https://example.com/docs/", "img/a.png", "https://example.com/docs/img/a.png");
    CHECK_RESOLVE(NULL, "rel/path", "rel/path");
    CHECK_RESOLVE("not-absolute", "rel/path", "rel/path");
    CHECK_RESOLVE("https://x.org/a", "mailto:me@x.org", "mailto:me@x.org");

    h2m_buf c = H2M_BUF_INIT;
    h2m_url_clean("  \n https://a.b/c\td e \t ", &c);
    CHECK_EQ_STR("https://a.b/cd e", c.data);
    h2m_buf_free(&c);

    CHECK(h2m_url_has_scheme("https://x"));
    CHECK(h2m_url_has_scheme("mailto:x"));
    CHECK(!h2m_url_has_scheme("/path:x"));
    CHECK(!h2m_url_has_scheme("1abc:x"));
    CHECK(h2m_url_scheme_is("JavaScript:alert(1)", "javascript"));
    CHECK(!h2m_url_scheme_is("javascripts:x", "javascript"));
}
