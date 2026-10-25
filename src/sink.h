/*
 * Markdown output sink.
 *
 * A sink accumulates Markdown for one container (the document, a list item,
 * a block quote, the content of an inline element, a table cell, ...). It
 * owns the rules that must hold regardless of which element produced the
 * text:
 *
 *   - HTML whitespace collapsing (runs of whitespace -> one space, no leading
 *     or trailing spaces on lines),
 *   - block separation (blank lines between blocks, collapsed "margins"),
 *   - hard line breaks,
 *   - context-sensitive escaping of Markdown syntax characters in text,
 *   - CommonMark emphasis flanking validation with lazy delimiter repair,
 *   - optional paragraph wrapping at safe break points.
 *
 * Separators and spaces are *requested* and only materialised when more
 * content follows, so nothing ever needs to be trimmed afterwards.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_SINK_H
#define H2M_SINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buf.h"

/* A pair of emphasis delimiters. `ch` is the delimiter character used for
 * flanking checks, or 0 for delimiters that always work (inline HTML). */
typedef struct h2m_delim {
    const char *open;
    const char *close;
    char ch;
} h2m_delim;

/* Settings shared by all sinks of one conversion. */
typedef struct h2m_sink_style {
    const char *hard_break; /* "\\\n" or "  \n" */
    bool gfm;               /* escape '~' (strikethrough) */
    bool nbsp_is_space;     /* treat U+00A0 as collapsible whitespace */
} h2m_sink_style;

typedef struct h2m_sink {
    h2m_buf buf;
    const h2m_sink_style *style;

    size_t line_start;         /* offset of the current line */
    bool initial_line_start;   /* offset 0 begins a Markdown line */

    int pending_nl;            /* requested block separation (1 or 2) */
    int pending_br;            /* requested hard line breaks */
    bool pending_space;        /* requested collapsible space */

    bool leading_space;        /* whitespace seen before any content */
    int leading_br;            /* hard breaks seen before any content */
    bool had_blank;            /* a blank line separated two blocks */

    bool flatten;              /* single-line mode (headings, cells, ...) */
    const char *flatten_sep;   /* replaces block separators and breaks when
                                  flattening; NULL means a space */
    bool escape_brackets;      /* also escape ']' (inside link text) */

    int wrap;                  /* wrap column (0 = off) */
    bool wrap_record_only;     /* collect break points for the parent only */
    size_t para_start;         /* start of the region still to be wrapped */
    size_t *breaks;            /* offsets of breakable spaces */
    size_t nbreaks;
    size_t cap_breaks;

    struct {
        bool active;
        size_t open_pos, open_len, close_pos, close_len;
        const h2m_delim *cands;
        int ncands;
        int chosen;
    } fix;                     /* emphasis awaiting its closing flank check */

    size_t last_list_end;      /* buffer length right after the last list */
    char last_list_marker;     /* marker character of that list */

    h2m_buf scratch;
} h2m_sink;

void h2m_sink_init(h2m_sink *s, const h2m_sink_style *style, bool line_start, int wrap);
void h2m_sink_free(h2m_sink *s);

/* Requests `n` newlines (1 = new line, 2 = blank line) before more content. */
void h2m_sink_block(h2m_sink *s, int n);

/* Requests a collapsible space. */
void h2m_sink_space(h2m_sink *s);

/* Requests a hard line break. */
void h2m_sink_br(h2m_sink *s);

/* Writes HTML text: collapses whitespace and escapes Markdown syntax. */
void h2m_sink_text(h2m_sink *s, const char *text, size_t len);

/* Writes pre-rendered inline Markdown verbatim (may contain newlines).
 * Returns the offset at which it was written. */
size_t h2m_sink_inline(h2m_sink *s, const char *md, size_t len);
size_t h2m_sink_inline_str(h2m_sink *s, const char *md);

/* Writes a pre-rendered block (after pending separators); verbatim. */
void h2m_sink_raw_block(h2m_sink *s, const char *md, size_t len);

/* Imports the wrap break points of an inline child written at `offset`. */
void h2m_sink_import_breaks(h2m_sink *s, const h2m_sink *child, size_t offset);

/* The character that will precede the next piece of content. */
uint32_t h2m_sink_prev_cp(const h2m_sink *s);

/* True when the next content will start a Markdown line. */
bool h2m_sink_at_line_start(const h2m_sink *s);

/* True when nothing has been written. */
static inline bool h2m_sink_empty(const h2m_sink *s)
{
    return s->buf.len == 0;
}

/*
 * Chooses the first delimiter pair that is valid given the surrounding
 * characters (the closer is assumed to be followed by whitespace; this is
 * re-checked lazily). Returns the index into cands.
 */
int h2m_sink_choose_delim(const h2m_sink *s, const h2m_delim *cands, int ncands,
                          const char *content, size_t len);

/* Registers an emphasis whose closer must be validated against the next
 * character written. */
void h2m_sink_set_fix(h2m_sink *s, size_t open_pos, size_t close_pos, const h2m_delim *cands, int ncands,
                      int chosen);

/* Completes the sink: resolves pending checks, wraps, trims. Trailing
 * requests stay readable in pending_space / pending_br. */
void h2m_sink_finish(h2m_sink *s);

/* Escapes a '!' at the end of the buffer (so a following '[' does not form
 * image syntax). */
void h2m_sink_guard_bang(h2m_sink *s);

#endif /* H2M_SINK_H */
