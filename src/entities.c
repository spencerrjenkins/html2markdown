/*
 * HTML character reference decoding.
 *
 * SPDX-License-Identifier: MIT
 */
#include "entities.h"

#include <string.h>

#include "encoding.h"
#include "entities_data.h"
#include "utf8.h"

static int compare_name(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0)
        return c;
    return (alen > blen) - (alen < blen);
}

static const struct h2m_entity_row *find_entity(const char *name, size_t len)
{
    size_t lo = 0, hi = H2M_ENTITY_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const struct h2m_entity_row *row = &h2m_entity_table[mid];
        int c = compare_name(name, len, row->name, row->len);
        if (c == 0)
            return row;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

bool h2m_entity_lookup(const char *name, size_t len, unsigned *cp1, unsigned *cp2)
{
    const struct h2m_entity_row *row = find_entity(name, len);
    if (!row)
        return false;
    if (cp1)
        *cp1 = row->cp1;
    if (cp2)
        *cp2 = row->cp2;
    return true;
}

static bool is_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

static void put_cp(h2m_buf *out, uint32_t cp)
{
    char tmp[4];
    size_t n = h2m_utf8_encode(cp, tmp);
    h2m_buf_append(out, tmp, n);
}

/* Numeric reference post-processing per the WHATWG "numeric character
 * reference end state". */
static uint32_t fix_numeric(uint32_t cp)
{
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return H2M_REPLACEMENT_CHAR;
    if (cp >= 0x80 && cp <= 0x9F)
        return h2m_windows1252_cp((unsigned char)cp);
    if (cp == 0x0D)
        return '\n';
    return cp;
}

size_t h2m_decode_charref(const char *s, size_t len, bool in_attribute, h2m_buf *out)
{
    if (len < 2 || s[0] != '&')
        return 0;

    if (s[1] == '#') {
        size_t i = 2;
        bool hex = false;
        if (i < len && (s[i] == 'x' || s[i] == 'X')) {
            hex = true;
            i++;
        }
        size_t digits_start = i;
        uint32_t cp = 0;
        bool overflow = false;
        while (i < len && (hex ? is_hex(s[i]) : (s[i] >= '0' && s[i] <= '9'))) {
            uint32_t d = (uint32_t)(hex ? hex_val(s[i]) : s[i] - '0');
            if (!overflow) {
                cp = cp * (hex ? 16u : 10u) + d;
                if (cp > 0x10FFFF)
                    overflow = true;
            }
            i++;
        }
        if (i == digits_start)
            return 0; /* "&#" or "&#x" with no digits: not a reference */
        if (i < len && s[i] == ';')
            i++;
        put_cp(out, overflow ? H2M_REPLACEMENT_CHAR : fix_numeric(cp));
        return i;
    }

    /* Named reference: collect the alphanumeric run. */
    size_t i = 1;
    while (i < len && i <= H2M_ENTITY_MAX_NAME && is_alnum(s[i]))
        i++;
    size_t run = i - 1;
    if (run == 0)
        return 0;

    /* Prefer the full name terminated by ';'. */
    if (i < len && s[i] == ';') {
        const struct h2m_entity_row *row = find_entity(s + 1, run + 1);
        if (row) {
            put_cp(out, row->cp1);
            if (row->cp2)
                put_cp(out, row->cp2);
            return run + 2;
        }
    }

    /* Otherwise the longest legacy name (no ';') that prefixes the run. */
    size_t max = run < H2M_ENTITY_LEGACY_MAX_NAME ? run : H2M_ENTITY_LEGACY_MAX_NAME;
    for (size_t n = max; n >= 2; n--) {
        const struct h2m_entity_row *row = find_entity(s + 1, n);
        if (!row)
            continue;
        if (in_attribute) {
            char next = (n + 1 < len) ? s[n + 1] : '\0';
            if (next == '=' || is_alnum(next))
                return 0;
        }
        put_cp(out, row->cp1);
        if (row->cp2)
            put_cp(out, row->cp2);
        return n + 1;
    }
    return 0;
}

void h2m_decode_entities(const char *s, size_t len, bool in_attribute, h2m_buf *out)
{
    size_t i = 0;
    while (i < len) {
        const char *amp = memchr(s + i, '&', len - i);
        if (!amp) {
            h2m_buf_append(out, s + i, len - i);
            return;
        }
        size_t pos = (size_t)(amp - s);
        h2m_buf_append(out, s + i, pos - i);
        size_t used = h2m_decode_charref(s + pos, len - pos, in_attribute, out);
        if (used == 0) {
            h2m_buf_putc(out, '&');
            used = 1;
        }
        i = pos + used;
    }
}
