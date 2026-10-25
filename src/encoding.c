/*
 * Input decoding and preprocessing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "encoding.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf8.h"

static const uint32_t cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

uint32_t h2m_windows1252_cp(unsigned char byte)
{
    if (byte >= 0x80 && byte <= 0x9F)
        return cp1252_high[byte - 0x80];
    return byte;
}

static uint32_t iso8859_15_cp(unsigned char byte)
{
    switch (byte) {
    case 0xA4: return 0x20AC;
    case 0xA6: return 0x0160;
    case 0xA8: return 0x0161;
    case 0xB4: return 0x017D;
    case 0xB8: return 0x017E;
    case 0xBC: return 0x0152;
    case 0xBD: return 0x0153;
    case 0xBE: return 0x0178;
    default: return byte;
    }
}

struct label {
    const char *name;
    h2m_encoding enc;
};

static const struct label labels[] = {
    {"ansi_x3.4-1968", H2M_ENC_WINDOWS_1252},
    {"ascii", H2M_ENC_WINDOWS_1252},
    {"cp1252", H2M_ENC_WINDOWS_1252},
    {"cp819", H2M_ENC_WINDOWS_1252},
    {"csisolatin1", H2M_ENC_WINDOWS_1252},
    {"csisolatin9", H2M_ENC_ISO_8859_15},
    {"csunicode", H2M_ENC_UTF16LE},
    {"ibm819", H2M_ENC_WINDOWS_1252},
    {"iso-10646-ucs-2", H2M_ENC_UTF16LE},
    {"iso-8859-1", H2M_ENC_WINDOWS_1252},
    {"iso-8859-15", H2M_ENC_ISO_8859_15},
    {"iso-ir-100", H2M_ENC_WINDOWS_1252},
    {"iso8859-1", H2M_ENC_WINDOWS_1252},
    {"iso8859-15", H2M_ENC_ISO_8859_15},
    {"iso88591", H2M_ENC_WINDOWS_1252},
    {"iso885915", H2M_ENC_ISO_8859_15},
    {"iso_8859-1", H2M_ENC_WINDOWS_1252},
    {"iso_8859-1:1987", H2M_ENC_WINDOWS_1252},
    {"iso_8859-15", H2M_ENC_ISO_8859_15},
    {"l1", H2M_ENC_WINDOWS_1252},
    {"l9", H2M_ENC_ISO_8859_15},
    {"latin1", H2M_ENC_WINDOWS_1252},
    {"latin9", H2M_ENC_ISO_8859_15},
    {"ucs-2", H2M_ENC_UTF16LE},
    {"unicode", H2M_ENC_UTF16LE},
    {"unicode-1-1-utf-8", H2M_ENC_UTF8},
    {"unicode11utf8", H2M_ENC_UTF8},
    {"unicode20utf8", H2M_ENC_UTF8},
    {"unicodefeff", H2M_ENC_UTF16LE},
    {"unicodefffe", H2M_ENC_UTF16BE},
    {"us-ascii", H2M_ENC_WINDOWS_1252},
    {"utf-16", H2M_ENC_UTF16LE},
    {"utf-16be", H2M_ENC_UTF16BE},
    {"utf-16le", H2M_ENC_UTF16LE},
    {"utf-8", H2M_ENC_UTF8},
    {"utf8", H2M_ENC_UTF8},
    {"windows-1252", H2M_ENC_WINDOWS_1252},
    {"x-cp1252", H2M_ENC_WINDOWS_1252},
    {"x-unicode20utf8", H2M_ENC_UTF8},
};

static bool is_ascii_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

h2m_encoding h2m_encoding_lookup(const char *label, size_t len)
{
    while (len && is_ascii_ws(*label)) {
        label++;
        len--;
    }
    while (len && is_ascii_ws(label[len - 1]))
        len--;
    if (len == 0 || len > 32)
        return H2M_ENC_UNKNOWN;
    char lower[33];
    for (size_t i = 0; i < len; i++)
        lower[i] = ascii_lower(label[i]);
    lower[len] = '\0';
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; i++) {
        if (strcmp(labels[i].name, lower) == 0)
            return labels[i].enc;
    }
    return H2M_ENC_UNKNOWN;
}

const char *h2m_encoding_name(h2m_encoding enc)
{
    switch (enc) {
    case H2M_ENC_UTF8: return "utf-8";
    case H2M_ENC_WINDOWS_1252: return "windows-1252";
    case H2M_ENC_ISO_8859_15: return "iso-8859-15";
    case H2M_ENC_UTF16LE: return "utf-16le";
    case H2M_ENC_UTF16BE: return "utf-16be";
    default: return "unknown";
    }
}

/* Case-insensitive search for an ASCII needle. */
static const char *find_ci(const char *hay, size_t hay_len, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0 || hay_len < n)
        return NULL;
    for (size_t i = 0; i + n <= hay_len; i++) {
        size_t j = 0;
        while (j < n && ascii_lower(hay[i + j]) == needle[j])
            j++;
        if (j == n)
            return hay + i;
    }
    return NULL;
}

/*
 * Simplified version of the WHATWG "prescan a byte stream to determine its
 * encoding" algorithm: look for charset= inside <meta ...> tags (or the
 * encoding pseudo-attribute of an XML declaration) in the first 1024 bytes.
 */
static bool prescan_charset(const char *in, size_t len, const char **label, size_t *label_len)
{
    size_t limit = len < 1024 ? len : 1024;
    size_t pos = 0;

    if (limit >= 5 && memcmp(in, "<?xml", 5) == 0) {
        const char *end = memchr(in, '>', limit);
        size_t decl_len = end ? (size_t)(end - in) : limit;
        const char *e = find_ci(in, decl_len, "encoding");
        if (e) {
            const char *p = e + 8;
            const char *stop = in + decl_len;
            while (p < stop && (is_ascii_ws(*p) || *p == '='))
                p++;
            if (p < stop && (*p == '"' || *p == '\''))
                p++;
            const char *start = p;
            while (p < stop && *p != '"' && *p != '\'' && !is_ascii_ws(*p))
                p++;
            if (p > start) {
                *label = start;
                *label_len = (size_t)(p - start);
                return true;
            }
        }
    }

    while (pos < limit) {
        const char *meta = find_ci(in + pos, limit - pos, "<meta");
        if (!meta)
            return false;
        size_t start = (size_t)(meta - in) + 5;
        const char *gt = memchr(in + start, '>', limit - start);
        size_t tag_end = gt ? (size_t)(gt - in) : limit;
        const char *cs = find_ci(in + start, tag_end - start, "charset");
        if (cs) {
            const char *p = cs + 7;
            const char *stop = in + tag_end;
            while (p < stop && is_ascii_ws(*p))
                p++;
            if (p < stop && *p == '=') {
                p++;
                while (p < stop && is_ascii_ws(*p))
                    p++;
                if (p < stop && (*p == '"' || *p == '\''))
                    p++;
                const char *vstart = p;
                while (p < stop && *p != '"' && *p != '\'' && *p != ';' && !is_ascii_ws(*p) &&
                       *p != '/')
                    p++;
                if (p > vstart) {
                    *label = vstart;
                    *label_len = (size_t)(p - vstart);
                    return true;
                }
            }
        }
        pos = tag_end;
    }
    return false;
}

/* Appends one code point with stream preprocessing applied. */
typedef struct {
    char *out;
    size_t len;
    bool pending_cr;
} writer;

static void put_cp(writer *w, uint32_t cp)
{
    if (cp == 0)
        return; /* NUL characters are dropped */
    if (cp == '\r') {
        w->out[w->len++] = '\n';
        w->pending_cr = true;
        return;
    }
    if (cp == '\n' && w->pending_cr) {
        w->pending_cr = false;
        return; /* second half of CRLF */
    }
    w->pending_cr = false;
    w->len += h2m_utf8_encode(cp, w->out + w->len);
}

char *h2m_decode_input(const char *in, size_t len, h2m_encoding forced,
                       size_t *out_len, h2m_encoding *used,
                       char *warn, size_t warn_size)
{
    const unsigned char *u = (const unsigned char *)in;
    h2m_encoding enc = H2M_ENC_UNKNOWN;
    size_t skip = 0;

    if (warn && warn_size)
        warn[0] = '\0';

    /* 1. Byte order mark wins over everything. */
    if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        enc = H2M_ENC_UTF8;
        skip = 3;
    } else if (len >= 2 && u[0] == 0xFF && u[1] == 0xFE) {
        enc = H2M_ENC_UTF16LE;
        skip = 2;
    } else if (len >= 2 && u[0] == 0xFE && u[1] == 0xFF) {
        enc = H2M_ENC_UTF16BE;
        skip = 2;
    }

    /* 2. Explicit request. */
    if (enc == H2M_ENC_UNKNOWN && forced != H2M_ENC_UNKNOWN)
        enc = forced;

    /* 3. <meta charset> prescan. */
    if (enc == H2M_ENC_UNKNOWN) {
        const char *label;
        size_t label_len;
        if (prescan_charset(in, len, &label, &label_len)) {
            enc = h2m_encoding_lookup(label, label_len);
            /* A document that claims UTF-16 without a BOM cannot have been
             * prescanned as ASCII, so the claim is wrong (WHATWG rule). */
            if (enc == H2M_ENC_UTF16LE || enc == H2M_ENC_UTF16BE)
                enc = H2M_ENC_UTF8;
            if (enc == H2M_ENC_UNKNOWN && warn && warn_size) {
                int n = (int)(label_len > 40 ? 40 : label_len);
                snprintf(warn, warn_size,
                         "unsupported document encoding '%.*s'; decoding as UTF-8", n, label);
                enc = H2M_ENC_UTF8;
            }
        }
    }

    /* 4. Fall back to UTF-8 when the bytes are valid UTF-8, else Windows-1252. */
    if (enc == H2M_ENC_UNKNOWN)
        enc = h2m_utf8_valid(in, len) ? H2M_ENC_UTF8 : H2M_ENC_WINDOWS_1252;

    in += skip;
    u += skip;
    len -= skip;

    /* Worst-case expansion: one input byte -> three UTF-8 bytes (U+FFFD or
     * a 1252 character); two UTF-16 bytes -> at most three UTF-8 bytes. */
    if (len > (SIZE_MAX - 8) / 3)
        return NULL;
    char *out = malloc(len * 3 + 8);
    if (!out)
        return NULL;
    writer w = {out, 0, false};

    switch (enc) {
    case H2M_ENC_UTF16LE:
    case H2M_ENC_UTF16BE: {
        bool le = enc == H2M_ENC_UTF16LE;
        size_t i = 0;
        while (i + 1 < len) {
            uint32_t unit = le ? (uint32_t)(u[i] | (u[i + 1] << 8)) : (uint32_t)((u[i] << 8) | u[i + 1]);
            i += 2;
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                if (i + 1 < len) {
                    uint32_t lo = le ? (uint32_t)(u[i] | (u[i + 1] << 8))
                                     : (uint32_t)((u[i] << 8) | u[i + 1]);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        i += 2;
                        put_cp(&w, 0x10000 + ((unit - 0xD800) << 10) + (lo - 0xDC00));
                        continue;
                    }
                }
                put_cp(&w, H2M_REPLACEMENT_CHAR);
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                put_cp(&w, H2M_REPLACEMENT_CHAR);
            } else {
                put_cp(&w, unit);
            }
        }
        if (i < len)
            put_cp(&w, H2M_REPLACEMENT_CHAR); /* odd trailing byte */
        break;
    }
    case H2M_ENC_WINDOWS_1252:
        for (size_t i = 0; i < len; i++)
            put_cp(&w, h2m_windows1252_cp(u[i]));
        break;
    case H2M_ENC_ISO_8859_15:
        for (size_t i = 0; i < len; i++)
            put_cp(&w, iso8859_15_cp(u[i]));
        break;
    default: {
        size_t i = 0;
        while (i < len) {
            if (u[i] < 0x80) {
                put_cp(&w, u[i]);
                i++;
                continue;
            }
            size_t adv;
            uint32_t cp = h2m_utf8_decode(in + i, len - i, &adv);
            put_cp(&w, cp);
            i += adv;
        }
        break;
    }
    }

    out[w.len] = '\0';
    if (out_len)
        *out_len = w.len;
    if (used)
        *used = enc;
    return out;
}
