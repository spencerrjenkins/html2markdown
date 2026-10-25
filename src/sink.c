/*
 * Markdown output sink.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sink.h"

#include <stdlib.h>
#include <string.h>

#include "entities.h"
#include "utf8.h"

void h2m_sink_init(h2m_sink *s, const h2m_sink_style *style, bool line_start, int wrap)
{
    memset(s, 0, sizeof *s);
    s->style = style;
    s->initial_line_start = line_start;
    s->wrap = wrap > 0 ? wrap : 0;
    s->last_list_end = (size_t)-1;
}

void h2m_sink_free(h2m_sink *s)
{
    h2m_buf_free(&s->buf);
    h2m_buf_free(&s->scratch);
    free(s->breaks);
    s->breaks = NULL;
    s->nbreaks = s->cap_breaks = 0;
}

/* ------------------------------------------------------------------------ */
/* Emphasis flanking rules (CommonMark 0.31, "delimiter runs")              */
/* ------------------------------------------------------------------------ */

static bool left_flanking(uint32_t prev, uint32_t next)
{
    if (h2m_cp_is_space(next))
        return false;
    if (!h2m_cp_is_punct(next))
        return true;
    return h2m_cp_is_space(prev) || h2m_cp_is_punct(prev);
}

static bool right_flanking(uint32_t prev, uint32_t next)
{
    if (h2m_cp_is_space(prev))
        return false;
    if (!h2m_cp_is_punct(prev))
        return true;
    return h2m_cp_is_space(next) || h2m_cp_is_punct(next);
}

static bool can_open(char ch, uint32_t prev, uint32_t next)
{
    bool lf = left_flanking(prev, next);
    if (ch == '_')
        return lf && (!right_flanking(prev, next) || h2m_cp_is_punct(prev));
    return lf;
}

static bool can_close(char ch, uint32_t prev, uint32_t next)
{
    bool rf = right_flanking(prev, next);
    if (ch == '_')
        return rf && (!left_flanking(prev, next) || h2m_cp_is_punct(next));
    return rf;
}

/* First/last character of emphasis content, looking through delimiter runs
 * of the same character (which merge with ours). */
static uint32_t content_edge(const char *s, size_t len, char skip, bool first)
{
    size_t start = 0, end = len;
    if (skip) {
        while (start < end && s[start] == skip)
            start++;
        while (end > start && s[end - 1] == skip)
            end--;
    }
    if (start >= end)
        return '\n';
    if (first) {
        size_t adv;
        return h2m_utf8_decode(s + start, end - start, &adv);
    }
    return h2m_utf8_decode_last(s + start, end - start);
}

static bool delim_ok(const h2m_delim *d, uint32_t prev, const char *content, size_t len, uint32_t next)
{
    if (!d->ch)
        return true;
    /* Adjacent runs of the same character would merge into one run. */
    if (prev == (uint32_t)(unsigned char)d->ch || next == (uint32_t)(unsigned char)d->ch)
        return false;
    uint32_t first = content_edge(content, len, d->ch, true);
    uint32_t last = content_edge(content, len, d->ch, false);
    return can_open(d->ch, prev, first) && can_close(d->ch, last, next);
}

static void shift_positions(h2m_sink *s, size_t open_pos, size_t old_open, size_t new_open,
                            size_t close_pos, size_t old_close, size_t new_close)
{
    /* Positions strictly after the old opener move by the opener delta;
     * positions after the old closer also move by the closer delta. */
#define SHIFT(p)                                                                \
    do {                                                                        \
        if ((p) != (size_t)-1 && (p) >= close_pos + old_close)                  \
            (p) = (p) - old_open - old_close + new_open + new_close;            \
        else if ((p) != (size_t)-1 && (p) >= open_pos + old_open)               \
            (p) = (p) - old_open + new_open;                                    \
    } while (0)
    for (size_t i = 0; i < s->nbreaks; i++)
        SHIFT(s->breaks[i]);
    SHIFT(s->line_start);
    SHIFT(s->para_start);
    SHIFT(s->last_list_end);
#undef SHIFT
}

static void resolve_fix(h2m_sink *s, uint32_t next)
{
    if (!s->fix.active)
        return;
    s->fix.active = false;
    const h2m_delim *cands = s->fix.cands;
    int chosen = s->fix.chosen;
    size_t open_pos = s->fix.open_pos, close_pos = s->fix.close_pos;
    size_t open_len = s->fix.open_len, close_len = s->fix.close_len;
    if (close_pos + close_len > s->buf.len || open_pos + open_len > close_pos)
        return; /* defensive: buffer changed underneath */

    uint32_t prev = open_pos ? h2m_utf8_decode_last(s->buf.data, open_pos) : '\n';
    const char *content = s->buf.data + open_pos + open_len;
    size_t clen = close_pos - open_pos - open_len;

    if (delim_ok(&cands[chosen], prev, content, clen, next))
        return;
    for (int i = 0; i < s->fix.ncands; i++) {
        if (i == chosen || !delim_ok(&cands[i], prev, content, clen, next))
            continue;
        const char *no = cands[i].open, *nc = cands[i].close;
        size_t nol = strlen(no), ncl = strlen(nc);
        h2m_buf_splice(&s->buf, close_pos, close_len, nc, ncl);
        h2m_buf_splice(&s->buf, open_pos, open_len, no, nol);
        shift_positions(s, open_pos, open_len, nol, close_pos, close_len, ncl);
        return;
    }
}

int h2m_sink_choose_delim(const h2m_sink *s, const h2m_delim *cands, int ncands,
                          const char *content, size_t len)
{
    uint32_t prev = h2m_sink_prev_cp(s);
    for (int i = 0; i < ncands; i++) {
        if (delim_ok(&cands[i], prev, content, len, ' '))
            return i;
    }
    return 0;
}

void h2m_sink_set_fix(h2m_sink *s, size_t open_pos, size_t close_pos, const h2m_delim *cands, int ncands,
                      int chosen)
{
    s->fix.active = true;
    s->fix.open_pos = open_pos;
    s->fix.open_len = strlen(cands[chosen].open);
    s->fix.close_pos = close_pos;
    s->fix.close_len = strlen(cands[chosen].close);
    s->fix.cands = cands;
    s->fix.ncands = ncands;
    s->fix.chosen = chosen;
}

/* ------------------------------------------------------------------------ */
/* Wrapping                                                                 */
/* ------------------------------------------------------------------------ */

static void record_break(h2m_sink *s, size_t pos)
{
    if (s->nbreaks == s->cap_breaks) {
        s->cap_breaks = s->cap_breaks ? s->cap_breaks * 2 : 32;
        s->breaks = h2m_xrealloc(s->breaks, s->cap_breaks * sizeof *s->breaks);
    }
    s->breaks[s->nbreaks++] = pos;
}

void h2m_sink_import_breaks(h2m_sink *s, const h2m_sink *child, size_t offset)
{
    if (!s->wrap || s->flatten)
        return;
    for (size_t i = 0; i < child->nbreaks; i++)
        record_break(s, offset + child->breaks[i]);
}

static bool all_same(const char *p, size_t n, char c)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != c)
            return false;
    }
    return true;
}

/* Would a line beginning with this word start a new block construct? */
static bool unsafe_line_start(const char *p, size_t n)
{
    size_t wl = 0;
    while (wl < n && p[wl] != ' ')
        wl++;
    if (wl == 0)
        return false;
    switch (p[0]) {
    case '#':
        return all_same(p, wl, '#') && wl <= 6;
    case '>':
        return true;
    case '-':
    case '*':
        return wl == 1 || all_same(p, wl, p[0]);
    case '+':
        return wl == 1;
    case '=':
    case '_':
        return all_same(p, wl, p[0]);
    case '`':
    case '~':
        return wl >= 3 && p[1] == p[0] && p[2] == p[0];
    default:
        if (p[0] >= '0' && p[0] <= '9') {
            size_t d = 0;
            while (d < wl && p[d] >= '0' && p[d] <= '9')
                d++;
            return d <= 9 && d + 1 == wl && (p[d] == '.' || p[d] == ')');
        }
        return false;
    }
}

static void apply_wrap(h2m_sink *s)
{
    if (s->wrap_record_only)
        return;
    size_t end = s->buf.len;
    if (s->wrap > 0 && s->nbreaks) {
        char *b = s->buf.data;
        size_t width = (size_t)s->wrap;
        size_t bi = 0;
        size_t line = s->para_start;
        while (line < end && bi < s->nbreaks) {
            size_t line_end = line;
            while (line_end < end && b[line_end] != '\n')
                line_end++;
            size_t pos = line, cur_w = 0, last_fit = (size_t)-1;
            for (; bi < s->nbreaks && s->breaks[bi] < line_end; bi++) {
                size_t c = s->breaks[bi];
                if (c < pos || b[c] != ' ')
                    continue;
                cur_w += h2m_utf8_width(b + pos, c - pos);
                pos = c;
                if (cur_w > width && last_fit != (size_t)-1) {
                    b[last_fit] = '\n';
                    line = last_fit + 1;
                    cur_w = h2m_utf8_width(b + line, c - line);
                    last_fit = (size_t)-1;
                }
                /* A backslash before the new line break would turn it into a
                 * hard line break. */
                size_t slashes = 0;
                while (c - slashes > line && b[c - slashes - 1] == '\\')
                    slashes++;
                if (slashes % 2 == 0 && !unsafe_line_start(b + c + 1, line_end - c - 1)) {
                    if (cur_w <= width) {
                        last_fit = c;
                    } else {
                        /* A single overlong segment: break right after it. */
                        b[c] = '\n';
                        line = c + 1;
                        cur_w = 0;
                        pos = c + 1;
                        continue;
                    }
                }
                cur_w += 1;
                pos = c + 1;
            }
            cur_w += h2m_utf8_width(b + pos, line_end - pos);
            if (cur_w > width && last_fit != (size_t)-1)
                b[last_fit] = '\n';
            line = line_end + 1;
        }
        for (size_t i = end; i > s->line_start; i--) {
            if (b[i - 1] == '\n') {
                s->line_start = i;
                break;
            }
        }
    }
    s->nbreaks = 0;
    s->para_start = end;
}

/* ------------------------------------------------------------------------ */
/* Pending separators                                                       */
/* ------------------------------------------------------------------------ */

uint32_t h2m_sink_prev_cp(const h2m_sink *s)
{
    if (s->buf.len == 0)
        return '\n';
    if (s->pending_nl || s->pending_br)
        return s->flatten ? ' ' : '\n';
    if (s->pending_space)
        return ' ';
    return h2m_utf8_decode_last(s->buf.data, s->buf.len);
}

bool h2m_sink_at_line_start(const h2m_sink *s)
{
    if (s->buf.len == 0)
        return s->initial_line_start;
    if (s->flatten)
        return false;
    if (s->pending_nl || s->pending_br)
        return true;
    if (s->pending_space)
        return false;
    return s->buf.len == s->line_start;
}

static void clear_pending(h2m_sink *s)
{
    s->pending_nl = 0;
    s->pending_br = 0;
    s->pending_space = false;
}

/* Materialises pending separators before content starting with `first`. */
static void flush_pending(h2m_sink *s, uint32_t first)
{
    if (s->fix.active) {
        uint32_t next = (s->pending_nl || s->pending_br) ? '\n' : s->pending_space ? ' ' : first;
        resolve_fix(s, next);
    }
    if (s->buf.len == 0) {
        clear_pending(s);
        return;
    }
    if (s->flatten) {
        if (s->pending_nl || s->pending_br) {
            if (s->flatten_sep) {
                h2m_buf_rtrim_spaces(&s->buf);
                h2m_buf_puts(&s->buf, s->flatten_sep);
            } else if (h2m_buf_last(&s->buf) != ' ') {
                h2m_buf_putc(&s->buf, ' ');
            }
        } else if (s->pending_space) {
            h2m_buf_putc(&s->buf, ' ');
        }
        clear_pending(s);
        return;
    }
    if (s->pending_nl || s->pending_br >= 2) {
        int n = s->pending_nl ? s->pending_nl : 2;
        apply_wrap(s);
        h2m_buf_rtrim_spaces(&s->buf);
        int have = 0;
        while ((size_t)have < s->buf.len && s->buf.data[s->buf.len - 1 - (size_t)have] == '\n')
            have++;
        for (; have < n; have++)
            h2m_buf_putc(&s->buf, '\n');
        if (n >= 2)
            s->had_blank = true;
        s->line_start = s->para_start = s->buf.len;
    } else if (s->pending_br) {
        h2m_buf_rtrim_spaces(&s->buf);
        h2m_buf_puts(&s->buf, s->style->hard_break);
        s->line_start = s->buf.len;
    } else if (s->pending_space) {
        if (s->wrap)
            record_break(s, s->buf.len);
        h2m_buf_putc(&s->buf, ' ');
    }
    clear_pending(s);
}

void h2m_sink_block(h2m_sink *s, int n)
{
    if (s->buf.len == 0)
        return;
    s->pending_space = false;
    s->pending_br = 0;
    if (n > s->pending_nl)
        s->pending_nl = n;
}

void h2m_sink_space(h2m_sink *s)
{
    if (s->buf.len == 0) {
        s->leading_space = true;
        return;
    }
    if (s->pending_nl || s->pending_br)
        return;
    char last = h2m_buf_last(&s->buf);
    if (last == ' ' || last == '\n')
        return;
    s->pending_space = true;
}

void h2m_sink_br(h2m_sink *s)
{
    if (s->buf.len == 0) {
        s->leading_br++;
        return;
    }
    if (s->pending_nl)
        return;
    s->pending_br++;
    s->pending_space = false;
}

/* ------------------------------------------------------------------------ */
/* Text                                                                     */
/* ------------------------------------------------------------------------ */

static size_t ws_len(const h2m_sink *s, const char *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        char c = p[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            i++;
            continue;
        }
        if (s->style->nbsp_is_space && (unsigned char)c == 0xC2 && i + 1 < n &&
            (unsigned char)p[i + 1] == 0xA0) {
            i += 2;
            continue;
        }
        break;
    }
    return i;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_ascii_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* True when p (starting with '&') would be parsed as a character reference. */
static bool looks_like_charref(const char *p, size_t n)
{
    size_t i = 1;
    if (i < n && p[i] == '#') {
        i++;
        bool hex = i < n && (p[i] == 'x' || p[i] == 'X');
        if (hex)
            i++;
        size_t start = i;
        while (i < n && (is_digit(p[i]) || (hex && ((p[i] >= 'a' && p[i] <= 'f') || (p[i] >= 'A' && p[i] <= 'F')))))
            i++;
        return i > start && i < n && p[i] == ';';
    }
    size_t start = i;
    while (i < n && (is_ascii_alpha(p[i]) || is_digit(p[i])))
        i++;
    if (i == start || i >= n || p[i] != ';')
        return false;
    return h2m_entity_lookup(p + start, i - start + 1, NULL, NULL);
}

/* Length of the current line prefix if it consists only of digits (for
 * ordered-list marker detection), or -1. */
static long line_digit_prefix(const h2m_sink *s, bool at_ls)
{
    if (at_ls)
        return 0;
    if (s->flatten || s->pending_space || s->pending_nl || s->pending_br)
        return -1;
    if (s->line_start == 0 && !s->initial_line_start)
        return -1;
    long count = 0;
    for (size_t i = s->line_start; i < s->buf.len; i++) {
        if (!is_digit(s->buf.data[i]))
            return -1;
        count++;
    }
    return count;
}

static void escape_word(const h2m_sink *s, const char *w, size_t n, bool ws_after, bool at_ls,
                        uint32_t prev, long digits, h2m_buf *out)
{
    size_t k = 0;
    while (k < n) {
        unsigned char c = (unsigned char)w[k];
        if (c >= 0x80) {
            size_t adv;
            uint32_t cp = h2m_utf8_decode(w + k, n - k, &adv);
            h2m_buf_append(out, w + k, adv);
            prev = cp;
            at_ls = false;
            digits = -1;
            k += adv;
            continue;
        }
        if (c < 0x20 || c == 0x7F) { /* stray control characters */
            k++;
            continue;
        }
        bool has_next = k + 1 < n;
        char nx = has_next ? w[k + 1] : '\0';
        bool next_ws = !has_next && ws_after;
        bool next_unknown = !has_next && !ws_after;
        bool esc = false;

        switch (c) {
        case '\\':
            esc = next_unknown || (has_next && h2m_is_ascii_punct(nx));
            break;
        case '`':
        case '[':
            esc = true;
            break;
        case ']':
            esc = s->escape_brackets;
            break;
        case '*':
            esc = at_ls || !(h2m_cp_is_space(prev) && next_ws);
            break;
        case '_': {
            uint32_t ncp = 0;
            if (has_next) {
                size_t adv;
                ncp = h2m_utf8_decode(w + k + 1, n - k - 1, &adv);
            }
            esc = !(has_next && h2m_cp_is_alnum(prev) && h2m_cp_is_alnum(ncp));
            break;
        }
        case '~':
            esc = s->style->gfm ? (at_ls || !(h2m_cp_is_space(prev) && next_ws)) : at_ls;
            break;
        case '<':
            esc = next_unknown || (has_next && (is_ascii_alpha(nx) || nx == '/' || nx == '!' || nx == '?'));
            break;
        case '&':
            esc = looks_like_charref(w + k, n - k);
            break;
        case '#':
            if (at_ls) {
                size_t r = 0;
                while (k + r < n && w[k + r] == '#')
                    r++;
                esc = r <= 6 && k + r == n;
            }
            break;
        case '>':
            esc = at_ls;
            break;
        case '-':
            esc = at_ls && (!has_next || all_same(w + k, n - k, '-'));
            break;
        case '+':
            esc = at_ls && !has_next;
            break;
        case '=':
            esc = at_ls && all_same(w + k, n - k, '=');
            break;
        case '.':
        case ')':
            esc = digits > 0 && digits <= 9 && !has_next;
            break;
        default:
            break;
        }
        if (esc)
            h2m_buf_putc(out, '\\');
        h2m_buf_putc(out, (char)c);
        if (is_digit((char)c) && digits >= 0)
            digits++;
        else
            digits = -1;
        at_ls = false;
        prev = c;
        k++;
    }
}

static void write_word(h2m_sink *s, const char *w, size_t n, bool ws_after)
{
    bool at_ls = h2m_sink_at_line_start(s);
    uint32_t prev = h2m_sink_prev_cp(s);
    long digits = line_digit_prefix(s, at_ls);
    h2m_buf_clear(&s->scratch);
    escape_word(s, w, n, ws_after, at_ls, prev, digits, &s->scratch);
    if (s->scratch.len == 0)
        return;
    size_t adv;
    uint32_t first = h2m_utf8_decode(s->scratch.data, s->scratch.len, &adv);
    flush_pending(s, first);
    h2m_buf_append(&s->buf, s->scratch.data, s->scratch.len);
}

void h2m_sink_text(h2m_sink *s, const char *t, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t w = ws_len(s, t + i, n - i);
        if (w) {
            h2m_sink_space(s);
            i += w;
            continue;
        }
        size_t j = i;
        while (j < n && ws_len(s, t + j, n - j) == 0) {
            if ((unsigned char)t[j] < 0x80) {
                j++;
            } else {
                size_t adv;
                h2m_utf8_decode(t + j, n - j, &adv);
                j += adv;
            }
        }
        write_word(s, t + i, j - i, j < n);
        i = j;
    }
}

size_t h2m_sink_inline(h2m_sink *s, const char *md, size_t len)
{
    if (len == 0)
        return s->buf.len;
    size_t adv;
    uint32_t first = h2m_utf8_decode(md, len, &adv);
    flush_pending(s, first);
    size_t pos = s->buf.len;
    h2m_buf_append(&s->buf, md, len);
    for (size_t i = len; i > 0; i--) {
        if (md[i - 1] == '\n') {
            s->line_start = pos + i;
            break;
        }
    }
    return pos;
}

size_t h2m_sink_inline_str(h2m_sink *s, const char *md)
{
    return h2m_sink_inline(s, md, strlen(md));
}

void h2m_sink_raw_block(h2m_sink *s, const char *md, size_t len)
{
    h2m_sink_inline(s, md, len);
    s->para_start = s->buf.len;
}

void h2m_sink_guard_bang(h2m_sink *s)
{
    if (s->pending_space || s->pending_nl || s->pending_br || h2m_buf_last(&s->buf) != '!')
        return;
    size_t i = s->buf.len - 1, slashes = 0;
    while (i > 0 && s->buf.data[i - 1] == '\\') {
        slashes++;
        i--;
    }
    if (slashes % 2 == 0)
        h2m_buf_splice(&s->buf, s->buf.len - 1, 0, "\\", 1);
}

void h2m_sink_finish(h2m_sink *s)
{
    resolve_fix(s, '\n');
    apply_wrap(s);
    while (s->buf.len) {
        char c = s->buf.data[s->buf.len - 1];
        if (c != ' ' && c != '\t' && c != '\n')
            break;
        s->buf.len--;
    }
    if (s->buf.data)
        s->buf.data[s->buf.len] = '\0';
    if (s->line_start > s->buf.len)
        s->line_start = s->buf.len;
}
