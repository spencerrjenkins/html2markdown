/*
 * Fuzz target for html2md.
 *
 * Build as a libFuzzer target (all library sources, without cli.c/main.c):
 *     clang -g -O1 -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
 *           tests/fuzz/fuzz_html2md.c $(LIB_SRCS)
 *
 * or as a self-contained mutation fuzzer (no libFuzzer required):
 *     make fuzz FUZZ_ITERATIONS=100000
 *     build-sanitize/fuzz_html2md ITERATIONS [SEED_FILE...]
 *
 * Every input is converted with option sets derived from its first byte, and
 * the output is checked for invariants: valid UTF-8, no NUL bytes, a single
 * trailing newline, and deterministic results.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html2md.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t need;
        if (c < 0x80) {
            i++;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
        } else {
            return 0;
        }
        for (size_t k = 1; k <= need; k++) {
            if (i + k >= n || (s[i + k] & 0xC0) != 0x80)
                return 0;
        }
        i += need + 1;
    }
    return 1;
}

static void check(const char *md, size_t len)
{
    if (!md) {
        fprintf(stderr, "fuzz: NULL output\n");
        abort();
    }
    if (strlen(md) != len) {
        fprintf(stderr, "fuzz: embedded NUL in output\n");
        abort();
    }
    if (!valid_utf8((const unsigned char *)md, len)) {
        fprintf(stderr, "fuzz: output is not valid UTF-8\n");
        abort();
    }
    if (len && md[len - 1] != '\n') {
        fprintf(stderr, "fuzz: output lacks a trailing newline\n");
        abort();
    }
    if (memchr(md, '\r', len)) {
        fprintf(stderr, "fuzz: carriage return in output\n");
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    h2m_options o;
    h2m_options_init(&o);
    unsigned char sel = size ? data[0] : 0;
    o.flavor = (sel & 1) ? H2M_FLAVOR_COMMONMARK : H2M_FLAVOR_GFM;
    o.raw_html = !(sel & 2);
    o.wrap = (sel & 4) ? 20 + (sel >> 4) : 0;
    o.link_style = (sel & 8) ? H2M_LINKS_REFERENCE : H2M_LINKS_INLINE;
    o.heading_style = (sel & 16) ? H2M_HEADING_SETEXT : H2M_HEADING_ATX;
    o.code_style = (sel & 32) ? H2M_CODE_INDENTED : H2M_CODE_FENCED;
    o.emphasis = (sel & 64) ? '_' : '*';
    o.main_content = (sel & 128) != 0;
    o.front_matter = (sel & 3) == 3;
    o.base_url = (sel & 5) == 5 ? "https://example.com/a/b" : NULL;

    char *md = NULL, *md2 = NULL;
    size_t len = 0, len2 = 0;
    if (h2m_convert((const char *)data, size, &o, &md, &len) != H2M_OK) {
        fprintf(stderr, "fuzz: conversion failed\n");
        abort();
    }
    check(md, len);
    if (h2m_convert((const char *)data, size, &o, &md2, &len2) != H2M_OK || len != len2 ||
        memcmp(md, md2, len) != 0) {
        fprintf(stderr, "fuzz: non-deterministic output\n");
        abort();
    }
    h2m_free(md);
    h2m_free(md2);
    return 0;
}

#ifdef H2M_STANDALONE_FUZZER

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static const char *const dict[] = {
    "<", ">", "</", "/>", "<p>", "</p>", "<div>", "</div>", "<b>", "</b>", "<i>", "</i>", "<em>",
    "<strong>", "</strong>", "<a href=\"x\">", "</a>", "<table>", "<tr>", "<td>", "<th>", "</td>",
    "</table>", "<ul>", "<ol start=3>", "<li>", "</li>", "<pre>", "</pre>", "<code>", "</code>",
    "<br>", "<hr>", "<img src=a alt=b>", "<h1>", "<h2>", "</h1>", "<blockquote>", "</blockquote>",
    "<!--", "-->", "<![CDATA[", "]]>", "<script>", "</script>", "<style>", "<svg>", "<math>",
    "<template>", "<select>", "<textarea>", "<plaintext>", "<title>", "&amp;", "&lt;", "&#",
    "&#x", ";", "&copy", "\"", "'", "=", " ", "\n", "\t", "*", "_", "`", "~", "#", "-", "+",
    "1.", "[", "]", "(", ")", "!", "\\", "|", "\xC2\xA0", "\xE4\xB8\xAD", "\xFF", "\x80",
    "<dl><dt>", "<dd>", "<sup>", "<kbd>", "<del>", "<details><summary>", "<caption>",
    "<input type=checkbox checked>", "<base href=/>", "<meta charset=latin1>", "<font>", "<nobr>",
};

static void mutate(unsigned char **buf, size_t *len, size_t *cap)
{
    int ops = 1 + (int)(rng() % 8);
    for (int k = 0; k < ops; k++) {
        if (*len + 64 > *cap) {
            *cap = (*len + 64) * 2;
            *buf = realloc(*buf, *cap);
            if (!*buf)
                abort();
        }
        unsigned char *b = *buf;
        size_t pos = *len ? (size_t)(rng() % (*len + 1)) : 0;
        switch (rng() % 6) {
        case 0: /* flip a byte */
            if (*len)
                b[rng() % *len] ^= (unsigned char)(1u << (rng() % 8));
            break;
        case 1: /* insert a random byte */
            memmove(b + pos + 1, b + pos, *len - pos);
            b[pos] = (unsigned char)rng();
            (*len)++;
            break;
        case 2: /* delete a range */
            if (*len) {
                size_t n = 1 + (size_t)(rng() % 16);
                if (pos >= *len)
                    pos = *len - 1;
                if (n > *len - pos)
                    n = *len - pos;
                memmove(b + pos, b + pos + n, *len - pos - n);
                *len -= n;
            }
            break;
        case 3:
        case 4: { /* insert a dictionary token */
            const char *t = dict[rng() % (sizeof dict / sizeof dict[0])];
            size_t n = strlen(t);
            memmove(b + pos + n, b + pos, *len - pos);
            memcpy(b + pos, t, n);
            *len += n;
            break;
        }
        default: /* duplicate a chunk */
            if (*len) {
                size_t start = (size_t)(rng() % *len);
                size_t n = 1 + (size_t)(rng() % 32);
                if (n > *len - start)
                    n = *len - start;
                if (*len + n + 64 > *cap) {
                    *cap = (*len + n + 64) * 2;
                    *buf = realloc(*buf, *cap);
                    if (!*buf)
                        abort();
                    b = *buf;
                }
                unsigned char tmp[32];
                memcpy(tmp, b + start, n);
                memmove(b + pos + n, b + pos, *len - pos);
                memcpy(b + pos, tmp, n);
                *len += n;
            }
            break;
        }
    }
    if (*len > 1 << 16)
        *len = 1 << 16; /* keep inputs small so iterations stay fast */
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    unsigned char *buf = malloc(cap);
    size_t r;
    while (buf && (r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    *len = n;
    return buf;
}

int main(int argc, char **argv)
{
    long iterations = argc > 1 ? strtol(argv[1], NULL, 10) : 10000;
    size_t nseeds = argc > 2 ? (size_t)(argc - 2) : 0;
    unsigned char **seeds = calloc(nseeds + 1, sizeof *seeds);
    size_t *seed_len = calloc(nseeds + 1, sizeof *seed_len);
    size_t loaded = 0;
    for (size_t i = 0; i < nseeds; i++) {
        seeds[loaded] = read_file(argv[i + 2], &seed_len[loaded]);
        if (seeds[loaded])
            loaded++;
    }
    if (loaded == 0) {
        static const char fallback[] = "<p>Hello <b>world</b> <a href=x>link</a></p><ul><li>a<li>b</ul>";
        seeds[0] = malloc(sizeof fallback);
        memcpy(seeds[0], fallback, sizeof fallback);
        seed_len[0] = sizeof fallback - 1;
        loaded = 1;
    }

    size_t cap = 1 << 17;
    unsigned char *buf = malloc(cap);
    for (long it = 0; it < iterations; it++) {
        size_t s = (size_t)(rng() % loaded);
        size_t len = seed_len[s] < cap - 64 ? seed_len[s] : cap - 64;
        memcpy(buf, seeds[s], len);
        mutate(&buf, &len, &cap);
        LLVMFuzzerTestOneInput(buf, len);
        if ((it + 1) % 5000 == 0)
            fprintf(stderr, "fuzz: %ld iterations\n", it + 1);
    }
    printf("fuzz: %ld iterations completed without failures\n", iterations);
    free(buf);
    for (size_t i = 0; i < loaded; i++)
        free(seeds[i]);
    free(seeds);
    free(seed_len);
    return 0;
}

#endif /* H2M_STANDALONE_FUZZER */
