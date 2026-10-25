/*
 * DOM to Markdown conversion rules.
 *
 * Conversion is a recursive walk over the DOM. Every element renders into
 * an h2m_sink (see sink.h), which takes care of whitespace, escaping and
 * block separation. Containers whose output needs post-processing (list
 * items, block quotes, emphasis, links, headings, table cells) render their
 * children into a child sink and then transform and append its content.
 *
 * SPDX-License-Identifier: MIT
 */
#include "convert.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sink.h"
#include "url.h"
#include "utf8.h"

#define MAX_RENDER_DEPTH 1024
#define MAX_CONTAINER_DEPTH 64   /* nested lists/quotes beyond this render flat */
#define MAX_TABLE_COLUMNS 1000
#define MAX_TABLE_CELLS 1000000

typedef struct {
    char *url;
    char *title;
} ref_entry;

enum {
    EMPH_STRONG = 1,
    EMPH_EM = 2,
    EMPH_STRIKE = 4
};

typedef struct ctx {
    const h2m_options *opt;
    h2m_sink_style style;
    bool gfm;
    bool raw_html;
    char *base; /* effective base URL, or NULL */

    ref_entry *refs;
    size_t nrefs;
    size_t cap_refs;

    int em_depth;
    int strong_depth;
    int strike_depth;
    int link_depth;
    int flatten;
    int render_depth;
    int container_depth; /* nesting of lists and block quotes */
    const char *cell_sep; /* flatten separator inside table cells */
    h2m_sink *li_sink;    /* sink of the list item being rendered */
    char li_bullet;       /* its bullet character (0 for ordered lists) */

    h2m_delim em[3];
    int n_em;
    h2m_delim strong[3];
    int n_strong;
    h2m_delim strike[2];
    int n_strike;
    char strong_open[3];
} ctx;

static void render_node(ctx *c, h2m_sink *s, h2m_node *n);
static void render_children(ctx *c, h2m_sink *s, h2m_node *n);
static void emit_code_block(ctx *c, h2m_sink *s, const char *code, size_t len, const h2m_buf *lang);
static void trim_code(const h2m_buf *raw, size_t *out_start, size_t *out_end);
static void code_language(const h2m_node *pre, h2m_buf *out);

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static bool is_element(const h2m_node *n)
{
    return n && n->type == H2M_NODE_ELEMENT;
}

static bool is_removed(const h2m_node *n)
{
    return (n->flags & H2M_NODE_F_REMOVED) != 0;
}

/* True for elements whose content never reaches the output. */
static bool is_dropped(const h2m_node *n)
{
    if (!is_element(n))
        return false;
    if (is_removed(n))
        return true;
    if (n->ns == H2M_NS_SVG)
        return true;
    if (n->ns != H2M_NS_HTML)
        return false;
    if (n->tag == TAG_SCRIPT)
        return false; /* may carry TeX (type="math/tex") */
    return h2m_tag_has(n->tag, TF_DROP);
}

static bool ascii_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

static bool ascii_nieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

static bool ascii_istarts(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        if (!*s || tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return false;
    }
    return true;
}

static bool is_html_ws(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static bool text_is_blank(const h2m_node *n)
{
    for (size_t i = 0; i < n->text_len; i++) {
        if (!is_html_ws(n->text[i]))
            return false;
    }
    return true;
}

/* Appends s with whitespace runs collapsed and trimmed. */
static void append_collapsed(h2m_buf *out, const char *s, size_t len)
{
    bool space = false;
    for (size_t i = 0; i < len; i++) {
        if (is_html_ws(s[i])) {
            space = out->len > 0;
            continue;
        }
        if (space) {
            h2m_buf_putc(out, ' ');
            space = false;
        }
        h2m_buf_putc(out, s[i]);
    }
    if (!out->data)
        h2m_buf_append(out, "", 0);
}

/* Concatenated text of a subtree, skipping dropped elements. `br` is
 * appended for <br> elements. */
static void collect_text(const h2m_node *root, h2m_buf *out, const char *br)
{
    if (root->type == H2M_NODE_TEXT) {
        h2m_buf_append(out, root->text, root->text_len);
        return;
    }
    const h2m_node *n = root->first_child;
    while (n) {
        bool skip = false;
        if (n->type == H2M_NODE_TEXT) {
            h2m_buf_append(out, n->text, n->text_len);
        } else if (is_element(n)) {
            if (is_dropped(n) || h2m_node_is(n, TAG_SCRIPT)) {
                skip = true;
            } else if (h2m_node_is(n, TAG_BR) && br) {
                h2m_buf_puts(out, br);
            }
        }
        n = h2m_node_next_in_tree(n, root, skip);
    }
    if (!out->data)
        h2m_buf_append(out, "", 0);
}

static void collect_text_collapsed(const h2m_node *root, h2m_buf *out)
{
    h2m_buf raw = H2M_BUF_INIT;
    collect_text(root, &raw, " ");
    append_collapsed(out, raw.data, raw.len);
    h2m_buf_free(&raw);
}

static bool has_block_descendant(const h2m_node *root)
{
    const h2m_node *n = root->first_child;
    while (n) {
        bool skip = false;
        if (is_element(n)) {
            if (is_dropped(n)) {
                skip = true;
            } else if (n->ns == H2M_NS_HTML && h2m_tag_has(n->tag, TF_BLOCK)) {
                return true;
            } else if (n->ns == H2M_NS_MATHML) {
                skip = true;
            }
        }
        n = h2m_node_next_in_tree(n, root, skip);
    }
    return false;
}

/* Parses the value of a CSS property from an inline style attribute into
 * `out` (lowercase, trimmed). Returns false when absent. */
static bool style_value(const h2m_node *n, const char *prop, char *out, size_t out_size)
{
    const char *style = h2m_node_attr(n, "style");
    if (!style || out_size == 0)
        return false;
    size_t plen = strlen(prop);
    const char *p = style;
    bool found = false;
    while (*p) {
        while (*p == ';' || is_html_ws(*p))
            p++;
        const char *name = p;
        while (*p && *p != ':' && *p != ';')
            p++;
        const char *name_end = p;
        while (name_end > name && is_html_ws(name_end[-1]))
            name_end--;
        if (*p != ':') {
            continue;
        }
        p++;
        while (is_html_ws(*p))
            p++;
        const char *val = p;
        while (*p && *p != ';')
            p++;
        const char *val_end = p;
        while (val_end > val && is_html_ws(val_end[-1]))
            val_end--;
        if ((size_t)(name_end - name) == plen && ascii_nieq(name, prop, plen)) {
            size_t n2 = (size_t)(val_end - val);
            if (n2 >= out_size)
                n2 = out_size - 1;
            for (size_t i = 0; i < n2; i++)
                out[i] = (char)tolower((unsigned char)val[i]);
            out[n2] = '\0';
            found = true; /* keep scanning: the last declaration wins */
        }
    }
    return found;
}

static bool is_hidden(const h2m_node *n)
{
    const char *hidden = h2m_node_attr(n, "hidden");
    if (hidden && !ascii_ieq(hidden, "until-found"))
        return true;
    const char *aria = h2m_node_attr(n, "aria-hidden");
    if (aria && ascii_ieq(aria, "true"))
        return true;
    char v[64];
    if (style_value(n, "display", v, sizeof v) && strncmp(v, "none", 4) == 0)
        return true;
    if (style_value(n, "visibility", v, sizeof v) && strncmp(v, "hidden", 6) == 0)
        return true;
    if (h2m_node_is(n, TAG_DIALOG) && !h2m_node_attr(n, "open"))
        return true;
    return false;
}

/* Resolves an attribute URL against the base URL. */
static void resolve_url(ctx *c, const char *raw, h2m_buf *out)
{
    h2m_buf clean = H2M_BUF_INIT;
    h2m_url_clean(raw, &clean);
    h2m_url_resolve(c->base, clean.data, out);
    h2m_buf_free(&clean);
    if (!out->data)
        h2m_buf_append(out, "", 0);
}

/* Formats a link destination so that it survives Markdown parsing. */
static void append_destination(h2m_buf *out, const char *url)
{
    size_t len = strlen(url);
    if (len == 0) {
        h2m_buf_puts(out, "<>");
        return;
    }
    int depth = 0;
    bool balanced = true;
    for (size_t i = 0; i < len; i++) {
        if (url[i] == '(')
            depth++;
        else if (url[i] == ')' && --depth < 0)
            balanced = false;
    }
    if (depth != 0)
        balanced = false;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)url[i];
        if (ch <= 0x20 || ch == 0x7F || ch == '<' || ch == '>' || ch == '\\') {
            h2m_buf_printf(out, "%%%02X", ch);
        } else if ((ch == '(' || ch == ')') && !balanced) {
            h2m_buf_putc(out, '\\');
            h2m_buf_putc(out, (char)ch);
        } else {
            h2m_buf_putc(out, (char)ch);
        }
    }
}

static void append_title(h2m_buf *out, const char *title)
{
    h2m_buf clean = H2M_BUF_INIT;
    append_collapsed(&clean, title, strlen(title));
    if (clean.len) {
        h2m_buf_puts(out, " \"");
        for (size_t i = 0; i < clean.len; i++) {
            char ch = clean.data[i];
            if (ch == '"' || ch == '\\')
                h2m_buf_putc(out, '\\');
            h2m_buf_putc(out, ch);
        }
        h2m_buf_putc(out, '"');
    }
    h2m_buf_free(&clean);
}

/* Starts a child sink for inline content of an element rendered into s. */
static void child_inline(ctx *c, const h2m_sink *parent, h2m_sink *child)
{
    h2m_sink_init(child, &c->style, false, parent->wrap);
    child->wrap_record_only = true;
    child->flatten = parent->flatten;
    child->flatten_sep = parent->flatten_sep;
    child->escape_brackets = parent->escape_brackets;
}

/* Starts a child sink for block content (list item, block quote). */
static void child_block(ctx *c, const h2m_sink *parent, h2m_sink *child, int indent)
{
    int wrap = 0;
    if (parent->wrap) {
        wrap = parent->wrap - indent;
        if (wrap < 20)
            wrap = 20;
    }
    h2m_sink_init(child, &c->style, true, wrap);
}

/* Emits an inline child's leading whitespace/breaks into s. */
static void emit_leading(h2m_sink *s, const h2m_sink *child)
{
    if (child->leading_space)
        h2m_sink_space(s);
    for (int i = 0; i < child->leading_br; i++)
        h2m_sink_br(s);
}

static void emit_trailing(h2m_sink *s, const h2m_sink *child)
{
    if (child->pending_space)
        h2m_sink_space(s);
    for (int i = 0; i < child->pending_br; i++)
        h2m_sink_br(s);
}

/* Writes an inline child's content wrapped in delimiters. */
static void emit_wrapped(h2m_sink *s, const h2m_sink *child, const h2m_delim *cands, int ncands)
{
    emit_leading(s, child);
    if (child->buf.len) {
        int i = h2m_sink_choose_delim(s, cands, ncands, child->buf.data, child->buf.len);
        size_t open_pos = h2m_sink_inline_str(s, cands[i].open);
        size_t content_pos = h2m_sink_inline(s, child->buf.data, child->buf.len);
        h2m_sink_import_breaks(s, child, content_pos);
        size_t close_pos = h2m_sink_inline_str(s, cands[i].close);
        h2m_sink_set_fix(s, open_pos, close_pos, cands, ncands, i);
    }
    emit_trailing(s, child);
}

/* Writes an inline child's content between fixed strings. */
static void emit_between(h2m_sink *s, const h2m_sink *child, const char *open, const char *close)
{
    emit_leading(s, child);
    if (child->buf.len) {
        h2m_sink_inline_str(s, open);
        size_t pos = h2m_sink_inline(s, child->buf.data, child->buf.len);
        h2m_sink_import_breaks(s, child, pos);
        h2m_sink_inline_str(s, close);
    }
    emit_trailing(s, child);
}

/* ------------------------------------------------------------------------ */
/* Inline elements                                                          */
/* ------------------------------------------------------------------------ */

static unsigned span_style_kinds(const h2m_node *n)
{
    unsigned kinds = 0;
    char v[64];
    if (style_value(n, "font-weight", v, sizeof v)) {
        if (strcmp(v, "bold") == 0 || strcmp(v, "bolder") == 0 || strcmp(v, "600") == 0 ||
            strcmp(v, "700") == 0 || strcmp(v, "800") == 0 || strcmp(v, "900") == 0)
            kinds |= EMPH_STRONG;
    }
    if (style_value(n, "font-style", v, sizeof v)) {
        if (strncmp(v, "italic", 6) == 0 || strncmp(v, "oblique", 7) == 0)
            kinds |= EMPH_EM;
    }
    if (style_value(n, "text-decoration", v, sizeof v) || style_value(n, "text-decoration-line", v, sizeof v)) {
        if (strstr(v, "line-through"))
            kinds |= EMPH_STRIKE;
    }
    return kinds;
}

static void render_emphasis(ctx *c, h2m_sink *s, h2m_node *n, unsigned kinds)
{
    unsigned kind = (kinds & EMPH_STRONG) ? EMPH_STRONG
                    : (kinds & EMPH_EM)    ? EMPH_EM
                    : (kinds & EMPH_STRIKE) ? EMPH_STRIKE
                                            : 0;
    if (kind == 0) {
        render_children(c, s, n);
        return;
    }
    unsigned rest = kinds & ~kind;
    int *depth = kind == EMPH_STRONG ? &c->strong_depth : kind == EMPH_EM ? &c->em_depth : &c->strike_depth;

    /* Nested emphasis of the same kind adds nothing; emphasis cannot span
     * blocks, so block content is rendered without it. */
    if (*depth > 0 || has_block_descendant(n)) {
        render_emphasis(c, s, n, rest);
        return;
    }
    const h2m_delim *cands;
    int ncands;
    if (kind == EMPH_STRONG) {
        cands = c->strong;
        ncands = c->n_strong;
    } else if (kind == EMPH_EM) {
        cands = c->em;
        ncands = c->n_em;
    } else {
        cands = c->strike;
        ncands = c->n_strike;
        if (ncands == 0) { /* no strikethrough available */
            render_emphasis(c, s, n, rest);
            return;
        }
    }

    h2m_sink child;
    child_inline(c, s, &child);
    (*depth)++;
    render_emphasis(c, &child, n, rest);
    (*depth)--;
    h2m_sink_finish(&child);
    emit_wrapped(s, &child, cands, ncands);
    h2m_sink_free(&child);
}

/* Inline HTML wrapper (<sup>, <sub>, <kbd>, ...). */
static void render_html_inline(ctx *c, h2m_sink *s, h2m_node *n, const char *tag)
{
    if (!c->raw_html || has_block_descendant(n)) {
        render_children(c, s, n);
        return;
    }
    char open[16], close[16];
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    h2m_sink child;
    child_inline(c, s, &child);
    render_children(c, &child, n);
    h2m_sink_finish(&child);
    emit_between(s, &child, open, close);
    h2m_sink_free(&child);
}

/* Writes `text` as a code span. */
static void emit_code_span(h2m_sink *s, const char *text, size_t len)
{
    size_t longest = 0, run = 0;
    for (size_t i = 0; i < len; i++) {
        run = text[i] == '`' ? run + 1 : 0;
        if (run > longest)
            longest = run;
    }
    size_t fence = longest + 1;
    bool pad = text[0] == '`' || text[len - 1] == '`' ||
               (text[0] == ' ' && text[len - 1] == ' ');

    /* A span that starts a line cannot be mistaken for a code fence: with a
     * fence of three or more backticks the content contains backticks, and
     * a fence's info string may not. */
    h2m_buf out = H2M_BUF_INIT;
    h2m_buf_repeat(&out, '`', fence);
    if (pad)
        h2m_buf_putc(&out, ' ');
    h2m_buf_append(&out, text, len);
    if (pad)
        h2m_buf_putc(&out, ' ');
    h2m_buf_repeat(&out, '`', fence);
    h2m_sink_inline(s, out.data, out.len);
    h2m_buf_free(&out);
}

/* Emits collapsed code text with flanking whitespace moved outside. */
static void emit_code_text(h2m_sink *s, const char *raw, size_t raw_len)
{
    size_t lead = 0;
    while (lead < raw_len && is_html_ws(raw[lead]))
        lead++;
    size_t end = raw_len;
    while (end > lead && is_html_ws(raw[end - 1]))
        end--;
    if (lead)
        h2m_sink_space(s);
    if (end > lead) {
        h2m_buf text = H2M_BUF_INIT;
        append_collapsed(&text, raw + lead, end - lead);
        emit_code_span(s, text.data, text.len);
        h2m_buf_free(&text);
    }
    if (end < raw_len && end > lead)
        h2m_sink_space(s);
}

static void emit_link(ctx *c, h2m_sink *s, const h2m_sink *text, const char *href, const char *title);

/* Returns the single meaningful child element of n, if any. */
static h2m_node *only_element_child(const h2m_node *n)
{
    h2m_node *found = NULL;
    for (h2m_node *ch = n->first_child; ch; ch = ch->next) {
        if (ch->type == H2M_NODE_TEXT) {
            for (size_t i = 0; i < ch->text_len; i++) {
                if (!is_html_ws(ch->text[i]))
                    return NULL;
            }
            continue;
        }
        if (!is_element(ch) || is_dropped(ch))
            continue;
        if (found)
            return NULL;
        found = ch;
    }
    return found;
}

/* A link worth keeping: not a script pseudo-URL and not a bare "#"
 * placeholder (typically wired to a JavaScript click handler). */
static bool link_is_usable(const h2m_node *a)
{
    const char *href = h2m_node_attr(a, "href");
    if (!href)
        return false;
    h2m_buf clean = H2M_BUF_INIT;
    h2m_url_clean(href, &clean);
    bool ok = !(h2m_url_scheme_is(clean.data, "javascript") || h2m_url_scheme_is(clean.data, "vbscript") ||
                h2m_url_scheme_is(clean.data, "data")) &&
              strcmp(clean.data, "#") != 0;
    h2m_buf_free(&clean);
    return ok;
}

static bool within_heading(const h2m_node *n)
{
    for (const h2m_node *p = n->parent; p; p = p->parent) {
        if (is_element(p) && p->ns == H2M_NS_HTML && h2m_tag_is_heading(p->tag))
            return true;
    }
    return false;
}

/* Heading permalinks ("¶", "#", link icons) generated by documentation
 * tools and GitHub are navigation noise. */
static bool is_permalink_anchor(const h2m_node *a)
{
    const char *href = h2m_node_attr(a, "href");
    if (!href || href[0] != '#')
        return false;
    static const char *const classes[] = {"anchor", "headerlink", "permalink", "hash-link", "heading-anchor",
                                          "anchor-link", "header-anchor", "anchorjs-link", "direct-link"};
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; i++) {
        if (h2m_node_has_class(a, classes[i]))
            return true;
    }
    const char *label = h2m_node_attr(a, "aria-label");
    if (label && ascii_istarts(label, "permalink"))
        return true;
    if (!within_heading(a))
        return false;
    h2m_buf text = H2M_BUF_INIT;
    collect_text(a, &text, " ");
    bool symbolic = true;
    for (size_t i = 0; i < text.len; i++) {
        unsigned char ch = (unsigned char)text.data[i];
        if (ch < 0x80 && isalnum(ch))
            symbolic = false;
    }
    bool short_text = h2m_utf8_width(text.data ? text.data : "", text.len) <= 2;
    h2m_buf_free(&text);
    return symbolic && short_text;
}

/* Collects the content of a <code> element as lines. Only <br> starts a new
 * line; all other whitespace collapses, as it does in any inline element. */
static void collect_code_lines(const h2m_node *root, h2m_buf *out)
{
    bool space = false;
    for (const h2m_node *n = root->first_child; n;) {
        bool skip = false;
        if (n->type == H2M_NODE_TEXT) {
            for (size_t i = 0; i < n->text_len; i++) {
                char ch = n->text[i];
                if (is_html_ws(ch)) {
                    space = true;
                    continue;
                }
                if (space && out->len && h2m_buf_last(out) != '\n')
                    h2m_buf_putc(out, ' ');
                space = false;
                h2m_buf_putc(out, ch);
            }
        } else if (is_element(n)) {
            if (is_dropped(n) || h2m_node_is(n, TAG_SCRIPT)) {
                skip = true;
            } else if (h2m_node_is(n, TAG_BR)) {
                h2m_buf_rtrim_spaces(out);
                h2m_buf_putc(out, '\n');
                space = false;
            }
        }
        n = h2m_node_next_in_tree(n, root, skip);
    }
    h2m_buf_rtrim_spaces(out);
    if (!out->data)
        h2m_buf_append(out, "", 0);
}

/* True when `n` is the only content of a block element, as in
 * "<p><code>line<br>line</code></p>". */
static bool is_lone_block_child(const h2m_node *n)
{
    const h2m_node *p = n->parent;
    if (!p || !is_element(p) || p->ns != H2M_NS_HTML || !h2m_tag_has(p->tag, TF_BLOCK))
        return false;
    return only_element_child(p) == n;
}

static void render_code(ctx *c, h2m_sink *s, h2m_node *n)
{
    /* A <code> broken into lines with <br> is a code block written with
     * inline markup; a code span cannot hold line breaks. */
    if (!c->flatten && !s->flatten && !c->link_depth && is_lone_block_child(n) &&
        h2m_node_find(n, TAG_BR)) {
        h2m_buf lines = H2M_BUF_INIT;
        collect_code_lines(n, &lines);
        size_t start, end;
        trim_code(&lines, &start, &end);
        if (end > start) {
            h2m_buf lang = H2M_BUF_INIT;
            code_language(n, &lang);
            emit_code_block(c, s, lines.data + start, end - start, &lang);
            h2m_buf_free(&lang);
        }
        h2m_buf_free(&lines);
        return;
    }

    /* <code><a href="...">name</a></code> reads better as a linked code
     * span: [`name`](...). */
    h2m_node *only = only_element_child(n);
    if (only && h2m_node_is(only, TAG_A) && !c->link_depth && c->opt->link_style != H2M_LINKS_TEXT &&
        link_is_usable(only) && !has_block_descendant(only)) {
        h2m_buf raw = H2M_BUF_INIT;
        collect_text(n, &raw, " ");
        h2m_sink text;
        child_inline(c, s, &text);
        emit_code_text(&text, raw.data, raw.len);
        h2m_sink_finish(&text);
        h2m_buf_free(&raw);
        if (text.buf.len) {
            h2m_buf href = H2M_BUF_INIT;
            resolve_url(c, h2m_node_attr(only, "href"), &href);
            emit_link(c, s, &text, href.data, h2m_node_attr(only, "title"));
            h2m_buf_free(&href);
        } else {
            emit_leading(s, &text);
        }
        h2m_sink_free(&text);
        return;
    }
    h2m_buf raw = H2M_BUF_INIT;
    collect_text(n, &raw, " ");
    emit_code_text(s, raw.data, raw.len);
    h2m_buf_free(&raw);
}

static size_t find_ref(ctx *c, const char *url, const char *title)
{
    for (size_t i = 0; i < c->nrefs; i++) {
        const char *t = c->refs[i].title ? c->refs[i].title : "";
        if (strcmp(c->refs[i].url, url) == 0 && strcmp(t, title ? title : "") == 0)
            return i + 1;
    }
    if (c->nrefs == c->cap_refs) {
        c->cap_refs = c->cap_refs ? c->cap_refs * 2 : 16;
        c->refs = h2m_xrealloc(c->refs, c->cap_refs * sizeof *c->refs);
    }
    c->refs[c->nrefs].url = h2m_xstrdup(url);
    c->refs[c->nrefs].title = title && *title ? h2m_xstrdup(title) : NULL;
    return ++c->nrefs;
}

/* Writes a link given its rendered text (an inline child sink). */
static void emit_link(ctx *c, h2m_sink *s, const h2m_sink *text, const char *href, const char *title)
{
    emit_leading(s, text);
    h2m_buf out = H2M_BUF_INIT;
    h2m_buf_putc(&out, '[');
    h2m_buf_append(&out, text->buf.data, text->buf.len);
    if (c->opt->link_style == H2M_LINKS_REFERENCE) {
        h2m_buf_printf(&out, "][%zu]", find_ref(c, href, title));
    } else {
        h2m_buf_puts(&out, "](");
        append_destination(&out, href);
        if (title && *title)
            append_title(&out, title);
        h2m_buf_putc(&out, ')');
    }
    h2m_sink_guard_bang(s);
    size_t pos = h2m_sink_inline(s, out.data, out.len);
    h2m_sink_import_breaks(s, text, pos + 1);
    h2m_buf_free(&out);
    emit_trailing(s, text);
}

static bool autolinkable(const char *url)
{
    if (!(h2m_url_scheme_is(url, "http") || h2m_url_scheme_is(url, "https") || h2m_url_scheme_is(url, "ftp") ||
          h2m_url_scheme_is(url, "mailto")))
        return false;
    for (const char *p = url; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch <= 0x20 || ch == '<' || ch == '>' || ch == 0x7F)
            return false;
    }
    return true;
}

static void render_link(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (is_permalink_anchor(n))
        return;
    const char *href_attr = h2m_node_attr(n, "href");
    if (!href_attr || c->link_depth || c->opt->link_style == H2M_LINKS_TEXT || !link_is_usable(n)) {
        render_children(c, s, n);
        return;
    }
    h2m_buf href = H2M_BUF_INIT;
    resolve_url(c, href_attr, &href);

    bool blocks = has_block_descendant(n);
    h2m_sink text;
    child_inline(c, s, &text);
    text.escape_brackets = true;
    if (blocks) {
        text.flatten = true;
        text.flatten_sep = NULL;
    }
    c->link_depth++;
    c->flatten += blocks;
    render_children(c, &text, n);
    c->flatten -= blocks;
    c->link_depth--;
    h2m_sink_finish(&text);

    const char *title = h2m_node_attr(n, "title");
    if (text.buf.len == 0) {
        /* Icon-only links: fall back to an accessible name. */
        const char *label = h2m_node_attr(n, "aria-label");
        if (!label || !*label)
            label = title;
        if (label && *label) {
            h2m_sink_text(&text, label, strlen(label));
            h2m_sink_finish(&text);
        }
    }

    if (text.buf.len) {
        h2m_buf plain = H2M_BUF_INIT;
        collect_text_collapsed(n, &plain);
        const char *shown = plain.data ? plain.data : "";
        bool is_auto = !(title && *title) && autolinkable(href.data) && c->opt->link_style == H2M_LINKS_INLINE &&
                       (strcmp(shown, href.data) == 0 ||
                        (h2m_url_scheme_is(href.data, "mailto") && strcmp(shown, href.data + 7) == 0 &&
                         strchr(shown, '@')));
        if (is_auto) {
            emit_leading(s, &text);
            h2m_buf out = H2M_BUF_INIT;
            h2m_buf_putc(&out, '<');
            h2m_buf_puts(&out, h2m_url_scheme_is(href.data, "mailto") ? shown : href.data);
            h2m_buf_putc(&out, '>');
            h2m_sink_inline(s, out.data, out.len);
            h2m_buf_free(&out);
            emit_trailing(s, &text);
        } else {
            emit_link(c, s, &text, href.data, title);
        }
        h2m_buf_free(&plain);
    } else {
        emit_leading(s, &text);
        emit_trailing(s, &text);
    }
    h2m_sink_free(&text);
    h2m_buf_free(&href);
}

/* Picks the best image source, following common lazy-loading patterns. */
static bool image_source(ctx *c, const h2m_node *n, h2m_buf *out)
{
    static const char *const attrs[] = {"src", "data-src", "data-lazy-src", "data-original", "data-url"};
    const char *chosen = NULL;
    for (size_t i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
        const char *v = h2m_node_attr(n, attrs[i]);
        if (!v)
            continue;
        h2m_buf clean = H2M_BUF_INIT;
        h2m_url_clean(v, &clean);
        bool usable = clean.len > 0 && (c->opt->keep_data_uris || !h2m_url_scheme_is(clean.data, "data"));
        h2m_buf_free(&clean);
        if (usable) {
            chosen = v;
            break;
        }
    }
    h2m_buf first = H2M_BUF_INIT;
    if (!chosen) {
        const char *srcset = h2m_node_attr(n, "srcset");
        if (!srcset)
            srcset = h2m_node_attr(n, "data-srcset");
        if (srcset) {
            const char *p = srcset;
            while (is_html_ws(*p))
                p++;
            const char *start = p;
            while (*p && !is_html_ws(*p) && *p != ',')
                p++;
            h2m_buf_append(&first, start, (size_t)(p - start));
            if (first.len && (c->opt->keep_data_uris || !h2m_url_scheme_is(first.data, "data")))
                chosen = first.data;
        }
    }
    if (chosen)
        resolve_url(c, chosen, out);
    h2m_buf_free(&first);
    return chosen != NULL;
}

static void render_image(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (c->opt->image_style == H2M_IMAGES_NONE)
        return;
    const char *w = h2m_node_attr(n, "width"), *h = h2m_node_attr(n, "height");
    if (w && h && (strcmp(w, "1") == 0 || strcmp(w, "0") == 0) && (strcmp(h, "1") == 0 || strcmp(h, "0") == 0))
        return; /* tracking pixel */

    const char *alt_attr = h2m_node_attr(n, "alt");
    h2m_buf alt = H2M_BUF_INIT;
    if (alt_attr)
        append_collapsed(&alt, alt_attr, strlen(alt_attr));

    h2m_buf src = H2M_BUF_INIT;
    bool has_src = image_source(c, n, &src);
    if (!has_src || c->opt->image_style == H2M_IMAGES_ALT) {
        if (alt.len)
            h2m_sink_text(s, alt.data, alt.len);
        h2m_buf_free(&alt);
        h2m_buf_free(&src);
        return;
    }

    h2m_sink alt_md;
    h2m_sink_init(&alt_md, &c->style, false, 0);
    alt_md.flatten = true;
    alt_md.escape_brackets = true;
    if (alt.len)
        h2m_sink_text(&alt_md, alt.data, alt.len);
    h2m_sink_finish(&alt_md);

    h2m_buf out = H2M_BUF_INIT;
    h2m_buf_puts(&out, "![");
    h2m_buf_append(&out, alt_md.buf.data ? alt_md.buf.data : "", alt_md.buf.len);
    const char *title = h2m_node_attr(n, "title");
    if (c->opt->link_style == H2M_LINKS_REFERENCE) {
        h2m_buf_printf(&out, "][%zu]", find_ref(c, src.data, title));
    } else {
        h2m_buf_puts(&out, "](");
        append_destination(&out, src.data);
        if (title && *title)
            append_title(&out, title);
        h2m_buf_putc(&out, ')');
    }
    h2m_sink_inline(s, out.data, out.len);

    h2m_buf_free(&out);
    h2m_sink_free(&alt_md);
    h2m_buf_free(&alt);
    h2m_buf_free(&src);
}

static void render_media(ctx *c, h2m_sink *s, h2m_node *n)
{
    const char *src = h2m_node_attr(n, "src");
    if (!src || !*src) {
        for (h2m_node *ch = n->first_child; ch && !src; ch = ch->next) {
            if (h2m_node_is(ch, TAG_SOURCE))
                src = h2m_node_attr(ch, "src");
        }
    }
    if (!src || !*src || c->opt->link_style == H2M_LINKS_TEXT)
        return;
    h2m_buf url = H2M_BUF_INIT;
    resolve_url(c, src, &url);
    if (h2m_url_scheme_is(url.data, "data")) {
        h2m_buf_free(&url);
        return;
    }
    const char *label = h2m_node_attr(n, "title");
    h2m_buf name = H2M_BUF_INIT;
    if (!label || !*label) {
        /* Use the file name from the URL. */
        const char *u = url.data;
        size_t end = strcspn(u, "?#");
        size_t start = end;
        while (start > 0 && u[start - 1] != '/')
            start--;
        if (end > start)
            h2m_buf_append(&name, u + start, end - start);
        else
            h2m_buf_puts(&name, h2m_node_is(n, TAG_AUDIO) ? "Audio" : "Video");
        label = name.data;
    }
    h2m_sink text;
    child_inline(c, s, &text);
    text.escape_brackets = true;
    h2m_sink_text(&text, label, strlen(label));
    h2m_sink_finish(&text);
    emit_link(c, s, &text, url.data, NULL);
    h2m_sink_free(&text);
    h2m_buf_free(&name);
    h2m_buf_free(&url);
}

static void emit_tex(ctx *c, h2m_sink *s, const char *tex, size_t len, bool display)
{
    h2m_buf t = H2M_BUF_INIT;
    size_t a = 0, b = len;
    while (a < b && is_html_ws(tex[a]))
        a++;
    while (b > a && is_html_ws(tex[b - 1]))
        b--;
    if (a == b) {
        h2m_buf_free(&t);
        return;
    }
    if (display && !c->flatten && !s->flatten) {
        /* Blank lines would end the $$ block in Markdown renderers. */
        h2m_buf_puts(&t, "$$\n");
        for (size_t i = a; i < b; i++) {
            if (tex[i] == '\n' && h2m_buf_last(&t) == '\n')
                continue;
            h2m_buf_putc(&t, tex[i]);
        }
        h2m_buf_puts(&t, "\n$$");
        h2m_sink_block(s, 2);
        h2m_sink_raw_block(s, t.data, t.len);
        h2m_sink_block(s, 2);
    } else {
        h2m_buf_putc(&t, '$');
        append_collapsed(&t, tex + a, b - a);
        h2m_buf_putc(&t, '$');
        h2m_sink_inline(s, t.data, t.len);
    }
    h2m_buf_free(&t);
}

static void render_math(ctx *c, h2m_sink *s, h2m_node *math)
{
    bool display = false;
    const char *d = h2m_node_attr(math, "display");
    if (d && ascii_ieq(d, "block"))
        display = true;
    for (h2m_node *n = math->first_child; n; n = h2m_node_next_in_tree(n, math, false)) {
        if (is_element(n) && strcmp(n->name, "annotation") == 0) {
            const char *enc = h2m_node_attr(n, "encoding");
            if (enc && (ascii_ieq(enc, "application/x-tex") || ascii_ieq(enc, "tex"))) {
                h2m_buf tex = H2M_BUF_INIT;
                collect_text(n, &tex, NULL);
                emit_tex(c, s, tex.data, tex.len, display);
                h2m_buf_free(&tex);
                return;
            }
        }
    }
    const char *alt = h2m_node_attr(math, "alttext");
    if (alt && *alt) {
        h2m_sink_text(s, alt, strlen(alt));
        return;
    }
    h2m_buf text = H2M_BUF_INIT;
    collect_text(math, &text, " ");
    h2m_sink_text(s, text.data, text.len);
    h2m_buf_free(&text);
}

static void render_input(ctx *c, h2m_sink *s, h2m_node *n)
{
    const char *type = h2m_node_attr(n, "type");
    if (!type || !ascii_ieq(type, "checkbox"))
        return;
    if (c->gfm && s == c->li_sink && h2m_sink_empty(s))
        h2m_sink_inline_str(s, h2m_node_attr(n, "checked") ? "[x] " : "[ ] ");
}

/* ------------------------------------------------------------------------ */
/* Block elements                                                           */
/* ------------------------------------------------------------------------ */

static void render_block(ctx *c, h2m_sink *s, h2m_node *n)
{
    h2m_sink_block(s, 2);
    render_children(c, s, n);
    h2m_sink_block(s, 2);
}

/* Prefixes every line of `text` (first line with `first`, others with
 * `rest`); empty lines receive the prefix without trailing spaces. */
static void prefix_lines(h2m_buf *out, const char *text, size_t len, const char *first, const char *rest)
{
    size_t i = 0;
    bool is_first = true;
    while (i <= len) {
        size_t j = i;
        while (j < len && text[j] != '\n')
            j++;
        const char *prefix = is_first ? first : rest;
        if (j > i) {
            h2m_buf_puts(out, prefix);
            h2m_buf_append(out, text + i, j - i);
        } else {
            /* blank line: keep only the non-space part of the prefix */
            size_t pl = strlen(prefix);
            while (pl && prefix[pl - 1] == ' ')
                pl--;
            h2m_buf_append(out, prefix, pl);
        }
        if (j < len)
            h2m_buf_putc(out, '\n');
        is_first = false;
        i = j + 1;
    }
}

/* Block elements that end a heading's text when they appear inside it
 * (typically an unclosed <hN> swallowing the rest of the document). */
static bool is_content_block(const h2m_node *n)
{
    if (!is_element(n) || n->ns != H2M_NS_HTML || is_dropped(n))
        return false;
    switch (n->tag) {
    case TAG_P: case TAG_UL: case TAG_OL: case TAG_DL: case TAG_TABLE: case TAG_PRE:
    case TAG_BLOCKQUOTE: case TAG_HR: case TAG_FIGURE: case TAG_DETAILS: case TAG_MENU:
    case TAG_H1: case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6:
        return true;
    default:
        return false;
    }
}

static void render_heading(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (c->flatten || s->flatten) {
        render_block(c, s, n);
        return;
    }
    int level = (int)(n->tag - TAG_H1) + 1;
    bool setext = c->opt->heading_style == H2M_HEADING_SETEXT && level <= 2;

    /* The heading text is the content before the first content block; a
     * leading <p> (as WYSIWYG editors produce) supplies it instead. */
    h2m_node *split = n->first_child;
    while (split && !is_content_block(split))
        split = split->next;
    h2m_node *text_end = split;
    h2m_node *text_start = n->first_child;
    bool leading_content = false;
    for (h2m_node *ch = n->first_child; ch != split; ch = ch->next) {
        if (is_element(ch) && !is_dropped(ch))
            leading_content = true;
        if (ch->type == H2M_NODE_TEXT && !text_is_blank(ch))
            leading_content = true;
    }
    if (!leading_content && split && h2m_node_is(split, TAG_P)) {
        text_start = split->first_child;
        text_end = NULL;
        split = split->next;
    }

    h2m_sink text;
    h2m_sink_init(&text, &c->style, setext, 0);
    text.flatten = true;
    c->flatten++;
    for (h2m_node *ch = text_start; ch != text_end; ch = ch->next)
        render_node(c, &text, ch);
    c->flatten--;
    h2m_sink_finish(&text);
    if (text.buf.len == 0) {
        h2m_sink_free(&text);
        h2m_sink_block(s, 2);
        for (h2m_node *ch = split; ch; ch = ch->next)
            render_node(c, s, ch);
        h2m_sink_block(s, 2);
        return;
    }

    h2m_buf out = H2M_BUF_INIT;
    if (setext) {
        h2m_buf_append(&out, text.buf.data, text.buf.len);
        h2m_buf_putc(&out, '\n');
        size_t w = h2m_utf8_width(text.buf.data, text.buf.len);
        h2m_buf_repeat(&out, level == 1 ? '=' : '-', w < 3 ? 3 : w);
    } else {
        h2m_buf_repeat(&out, '#', (size_t)level);
        h2m_buf_putc(&out, ' ');
        /* A trailing run of '#' preceded by a space would be read as the
         * optional closing sequence. */
        size_t len = text.buf.len, run = 0;
        while (run < len && text.buf.data[len - 1 - run] == '#')
            run++;
        if (run && (run == len || text.buf.data[len - 1 - run] == ' ')) {
            h2m_buf_append(&out, text.buf.data, len - run);
            h2m_buf_putc(&out, '\\');
            h2m_buf_append(&out, text.buf.data + len - run, run);
        } else {
            h2m_buf_append(&out, text.buf.data, len);
        }
    }
    h2m_sink_block(s, 2);
    h2m_sink_raw_block(s, out.data, out.len);
    h2m_sink_block(s, 2);
    h2m_buf_free(&out);
    h2m_sink_free(&text);
    for (h2m_node *ch = split; ch; ch = ch->next)
        render_node(c, s, ch);
    h2m_sink_block(s, 2);
}

static void render_blockquote(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (c->flatten || s->flatten || c->container_depth >= MAX_CONTAINER_DEPTH) {
        render_block(c, s, n);
        return;
    }
    h2m_sink inner;
    child_block(c, s, &inner, 2);
    h2m_sink *saved_li = c->li_sink;
    c->li_sink = NULL;
    c->container_depth++;
    render_children(c, &inner, n);
    c->container_depth--;
    c->li_sink = saved_li;
    h2m_sink_finish(&inner);
    if (inner.buf.len) {
        h2m_buf out = H2M_BUF_INIT;
        prefix_lines(&out, inner.buf.data, inner.buf.len, "> ", "> ");
        h2m_sink_block(s, 2);
        h2m_sink_raw_block(s, out.data, out.len);
        h2m_sink_block(s, 2);
        h2m_buf_free(&out);
    }
    h2m_sink_free(&inner);
}

static void render_hr(ctx *c, h2m_sink *s)
{
    if (c->flatten || s->flatten) {
        h2m_sink_block(s, 2);
        return;
    }
    /* "- ---" would itself be a thematic break, not an item containing one. */
    bool clash = s == c->li_sink && h2m_sink_empty(s) && c->li_bullet == '-';
    h2m_sink_block(s, 2);
    h2m_sink_raw_block(s, clash ? "***" : "---", 3);
    h2m_sink_block(s, 2);
}

static bool is_list(const h2m_node *n)
{
    return h2m_node_is(n, TAG_UL) || h2m_node_is(n, TAG_OL) || h2m_node_is(n, TAG_MENU) || h2m_node_is(n, TAG_DIR);
}

static long parse_long(const char *s, long fallback)
{
    if (!s)
        return fallback;
    while (is_html_ws(*s))
        s++;
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s)
        return fallback;
    if (v < 0)
        v = 0;
    if (v > 999999999L)
        v = 999999999L;
    return v;
}

typedef struct {
    h2m_node *li;          /* the list item (or other element acting as one) */
    h2m_node **extra;      /* nested lists that followed it directly */
    size_t nextra;
} list_item;

static void render_list(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (c->flatten || s->flatten || c->container_depth >= MAX_CONTAINER_DEPTH) {
        render_block(c, s, n);
        return;
    }
    bool ordered = h2m_node_is(n, TAG_OL);

    /* Gather items. Lists nested directly inside a list (invalid but
     * common) belong to the preceding item. */
    list_item *items = NULL;
    size_t nitems = 0, cap = 0;
    h2m_buf stray = H2M_BUF_INIT;
    for (h2m_node *ch = n->first_child; ch; ch = ch->next) {
        if (ch->type == H2M_NODE_TEXT) {
            if (!text_is_blank(ch))
                h2m_buf_append(&stray, ch->text, ch->text_len);
            continue;
        }
        if (!is_element(ch) || is_dropped(ch))
            continue;
        if (is_list(ch) && nitems > 0) {
            list_item *it = &items[nitems - 1];
            it->extra = h2m_xrealloc(it->extra, (it->nextra + 1) * sizeof *it->extra);
            it->extra[it->nextra++] = ch;
            continue;
        }
        if (nitems == cap) {
            cap = cap ? cap * 2 : 8;
            items = h2m_xrealloc(items, cap * sizeof *items);
        }
        items[nitems].li = ch;
        items[nitems].extra = NULL;
        items[nitems].nextra = 0;
        nitems++;
    }
    if (nitems == 0) {
        if (stray.len)
            h2m_sink_text(s, stray.data, stray.len);
        h2m_buf_free(&stray);
        free(items);
        return;
    }
    h2m_buf_free(&stray);

    long number = ordered ? parse_long(h2m_node_attr(n, "start"), 1) : 0;
    bool reversed = ordered && h2m_node_attr(n, "reversed");
    if (reversed && !h2m_node_attr(n, "start"))
        number = (long)nitems;

    /* Consecutive lists would merge into one; alternate the marker. */
    char bullet = c->opt->bullet;
    char delim = '.';
    bool adjacent = s->last_list_end == s->buf.len && !h2m_sink_empty(s);
    if (adjacent) {
        if (ordered && s->last_list_marker == '.')
            delim = ')';
        else if (!ordered && s->last_list_marker == bullet)
            bullet = bullet == '-' ? '*' : '-';
    }

    h2m_buf body = H2M_BUF_INIT;
    h2m_buf *rendered = h2m_xcalloc(nitems, sizeof *rendered);
    char (*markers)[24] = h2m_xcalloc(nitems, sizeof *markers);
    bool loose = false;
    long first_number = number;

    for (size_t i = 0; i < nitems; i++) {
        h2m_node *li = items[i].li;
        if (ordered) {
            if (h2m_node_is(li, TAG_LI) && h2m_node_attr(li, "value"))
                number = parse_long(h2m_node_attr(li, "value"), number);
            if (i == 0)
                first_number = number;
            snprintf(markers[i], sizeof markers[i], "%ld%c", number, delim);
            number += reversed ? -1 : 1;
            if (number < 0)
                number = 0;
        } else {
            markers[i][0] = bullet;
            markers[i][1] = '\0';
        }
        int indent = (int)strlen(markers[i]) + 1;

        h2m_sink item;
        child_block(c, s, &item, indent);
        h2m_sink *saved_li = c->li_sink;
        char saved_bullet = c->li_bullet;
        c->li_sink = &item;
        c->li_bullet = ordered ? 0 : bullet;
        c->container_depth++;
        if (h2m_node_is(li, TAG_LI))
            render_children(c, &item, li);
        else
            render_node(c, &item, li);
        for (size_t k = 0; k < items[i].nextra; k++)
            render_node(c, &item, items[i].extra[k]);
        c->container_depth--;
        c->li_sink = saved_li;
        c->li_bullet = saved_bullet;
        h2m_sink_finish(&item);
        if (item.had_blank)
            loose = true;
        rendered[i] = item.buf;
        h2m_buf_init(&item.buf);
        h2m_sink_free(&item);
    }

    for (size_t i = 0; i < nitems; i++) {
        if (i > 0)
            h2m_buf_puts(&body, loose ? "\n\n" : "\n");
        char first[32], rest[32];
        size_t mlen = strlen(markers[i]);
        if (rendered[i].len) {
            snprintf(first, sizeof first, "%s ", markers[i]);
            memset(rest, ' ', mlen + 1);
            rest[mlen + 1] = '\0';
            prefix_lines(&body, rendered[i].data, rendered[i].len, first, rest);
        } else {
            h2m_buf_puts(&body, markers[i]);
        }
        h2m_buf_free(&rendered[i]);
    }

    /* A list nested in a list item follows the item's text on the next line,
     * unless it could not interrupt a paragraph there. */
    bool in_item = s == c->li_sink && !h2m_sink_empty(s);
    bool can_interrupt = !ordered || first_number == 1;
    int sep = (in_item && can_interrupt) ? 1 : 2;
    h2m_sink_block(s, sep);
    h2m_sink_raw_block(s, body.data, body.len);
    s->last_list_end = s->buf.len;
    s->last_list_marker = ordered ? delim : bullet;
    h2m_sink_block(s, sep);

    h2m_buf_free(&body);
    free(rendered);
    free(markers);
    for (size_t i = 0; i < nitems; i++)
        free(items[i].extra);
    free(items);
}

/* Programming language hint for a code block, from common class names. */
static bool class_language(const h2m_node *n, h2m_buf *out)
{
    const char *attrs[] = {"data-lang", "data-language"};
    for (size_t i = 0; i < 2; i++) {
        const char *v = h2m_node_attr(n, attrs[i]);
        if (v && *v) {
            h2m_buf_puts(out, v);
            return true;
        }
    }
    const char *cls = h2m_node_attr(n, "class");
    if (!cls)
        return false;
    static const char *const prefixes[] = {"language-", "lang-", "highlight-source-", "highlight-text-",
                                           "highlight-", "brush:"};
    const char *p = cls;
    while (*p) {
        while (is_html_ws(*p))
            p++;
        const char *start = p;
        while (*p && !is_html_ws(*p))
            p++;
        size_t tl = (size_t)(p - start);
        for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
            size_t pl = strlen(prefixes[i]);
            if (tl > pl && strncmp(start, prefixes[i], pl) == 0) {
                h2m_buf_append(out, start + pl, tl - pl);
                return true;
            }
            if (tl == pl && strncmp(start, prefixes[i], pl) == 0 && strcmp(prefixes[i], "brush:") == 0) {
                /* "brush: js" with a space */
                while (is_html_ws(*p))
                    p++;
                const char *v = p;
                while (*p && !is_html_ws(*p) && *p != ';')
                    p++;
                if (p > v) {
                    h2m_buf_append(out, v, (size_t)(p - v));
                    return true;
                }
            }
        }
    }
    return false;
}

static void code_language(const h2m_node *pre, h2m_buf *out)
{
    h2m_buf raw = H2M_BUF_INIT;
    const h2m_node *code = only_element_child(pre);
    bool found = false;
    if (code && (h2m_node_is(code, TAG_CODE) || h2m_node_is(code, TAG_SAMP) || h2m_node_is(code, TAG_KBD)))
        found = class_language(code, &raw);
    const h2m_node *n = pre;
    for (int depth = 0; !found && n && is_element(n) && depth < 3; depth++, n = n->parent)
        found = class_language(n, &raw);
    if (found) {
        for (size_t i = 0; i < raw.len; i++) {
            char ch = raw.data[i];
            if (isalnum((unsigned char)ch) || (unsigned char)ch >= 0x80 || ch == '_' || ch == '+' || ch == '#' ||
                ch == '.' || ch == '-')
                h2m_buf_putc(out, ch);
            else
                break;
        }
        if (out->len && (ascii_ieq(out->data, "none") || ascii_ieq(out->data, "nohighlight") ||
                         ascii_ieq(out->data, "plaintext") || ascii_ieq(out->data, "plain")))
            h2m_buf_clear(out);
        /* GitHub (linguist) text scopes that differ from fence names. */
        static const char *const aliases[][2] = {
            {"shell-session", "console"}, {"html-basic", "html"}, {"md", "markdown"}, {"tex-latex", "latex"}};
        for (size_t i = 0; out->len && i < sizeof aliases / sizeof aliases[0]; i++) {
            if (strcmp(out->data, aliases[i][0]) == 0) {
                h2m_buf_clear(out);
                h2m_buf_puts(out, aliases[i][1]);
                break;
            }
        }
    }
    h2m_buf_free(&raw);
}

/* Trims leading blank lines and trailing blank lines from code text. */
static void trim_code(const h2m_buf *raw, size_t *out_start, size_t *out_end)
{
    size_t start = 0, line = 0;
    while (line < raw->len) {
        size_t j = line;
        while (j < raw->len && (raw->data[j] == ' ' || raw->data[j] == '\t'))
            j++;
        if (j < raw->len && raw->data[j] == '\n') {
            line = j + 1;
            start = line;
            continue;
        }
        break;
    }
    /* Drop trailing blank lines but keep trailing spaces of the last line. */
    size_t end = raw->len;
    for (;;) {
        while (end > start && raw->data[end - 1] == '\n')
            end--;
        size_t k = end;
        while (k > start && (raw->data[k - 1] == ' ' || raw->data[k - 1] == '\t'))
            k--;
        if (k > start && raw->data[k - 1] == '\n') {
            end = k; /* the last line was whitespace only */
            continue;
        }
        if (k == start)
            end = start;
        break;
    }
    *out_start = start;
    *out_end = end;
}

static void render_pre(ctx *c, h2m_sink *s, h2m_node *n)
{
    h2m_buf raw = H2M_BUF_INIT;
    collect_text(n, &raw, "\n");
    size_t start, end;
    trim_code(&raw, &start, &end);
    if (end <= start) {
        h2m_buf_free(&raw);
        return;
    }
    const char *code = raw.data + start;
    size_t len = end - start;

    if (c->flatten || s->flatten) {
        emit_code_text(s, code, len);
        h2m_buf_free(&raw);
        return;
    }

    h2m_buf lang = H2M_BUF_INIT;
    code_language(n, &lang);
    emit_code_block(c, s, code, len, &lang);
    h2m_buf_free(&lang);
    h2m_buf_free(&raw);
}

/* Writes a fenced (or indented) code block. */
static void emit_code_block(ctx *c, h2m_sink *s, const char *code, size_t len, const h2m_buf *lang)
{
    h2m_buf out = H2M_BUF_INIT;
    bool after_list = s->last_list_end == s->buf.len && !h2m_sink_empty(s);
    bool indented = c->opt->code_style == H2M_CODE_INDENTED && !after_list;
    if (indented) {
        prefix_lines(&out, code, len, "    ", "    ");
    } else {
        char fc = c->opt->fence;
        size_t longest = 0;
        size_t i = 0;
        while (i < len) {
            size_t j = i, sp = 0;
            while (j < len && code[j] == ' ' && sp < 4) {
                j++;
                sp++;
            }
            size_t run = 0;
            while (j + run < len && code[j + run] == fc)
                run++;
            if (sp < 4 && run > longest)
                longest = run;
            while (i < len && code[i] != '\n')
                i++;
            i++;
        }
        size_t fence = longest >= 3 ? longest + 1 : 3;
        h2m_buf_repeat(&out, fc, fence);
        if (lang->len)
            h2m_buf_append(&out, lang->data, lang->len);
        h2m_buf_putc(&out, '\n');
        h2m_buf_append(&out, code, len);
        h2m_buf_putc(&out, '\n');
        h2m_buf_repeat(&out, fc, fence);
    }
    h2m_sink_block(s, 2);
    h2m_sink_raw_block(s, out.data, out.len);
    h2m_sink_block(s, 2);
    h2m_buf_free(&out);
}

/* ------------------------------------------------------------------------ */
/* Tables                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    char *text;
    size_t len;
    int align; /* 0 none, 1 left, 2 center, 3 right */
} cell;

typedef struct {
    cell *cells;
    size_t n;
    size_t cap;
    bool header;
} row;

static void row_add(row *r, char *text, size_t len, int align)
{
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 8;
        r->cells = h2m_xrealloc(r->cells, r->cap * sizeof *r->cells);
    }
    r->cells[r->n].text = text;
    r->cells[r->n].len = len;
    r->cells[r->n].align = align;
    r->n++;
}

static int cell_align(const h2m_node *cellnode)
{
    const char *a = h2m_node_attr(cellnode, "align");
    char v[32];
    if (!a && style_value(cellnode, "text-align", v, sizeof v))
        a = v;
    if (!a)
        return 0;
    if (ascii_istarts(a, "left") || ascii_istarts(a, "start"))
        return 1;
    if (ascii_istarts(a, "center"))
        return 2;
    if (ascii_istarts(a, "right") || ascii_istarts(a, "end"))
        return 3;
    return 0;
}

static void collect_rows(h2m_node *table, h2m_node ***out, size_t *nout, h2m_node **caption)
{
    h2m_node **head = NULL, **body = NULL, **foot = NULL;
    size_t nh = 0, nb = 0, nf = 0;
    *caption = NULL;
    for (h2m_node *ch = table->first_child; ch; ch = ch->next) {
        if (!is_element(ch) || is_removed(ch))
            continue;
        if (h2m_node_is(ch, TAG_CAPTION)) {
            if (!*caption)
                *caption = ch;
            continue;
        }
        h2m_node ***dst;
        size_t *nd;
        if (h2m_node_is(ch, TAG_THEAD)) {
            dst = &head;
            nd = &nh;
        } else if (h2m_node_is(ch, TAG_TFOOT)) {
            dst = &foot;
            nd = &nf;
        } else {
            dst = &body;
            nd = &nb;
        }
        if (h2m_node_is(ch, TAG_TR)) {
            *dst = h2m_xrealloc(*dst, (*nd + 1) * sizeof **dst);
            (*dst)[(*nd)++] = ch;
        } else if (h2m_node_is(ch, TAG_THEAD) || h2m_node_is(ch, TAG_TBODY) || h2m_node_is(ch, TAG_TFOOT)) {
            for (h2m_node *tr = ch->first_child; tr; tr = tr->next) {
                if (h2m_node_is(tr, TAG_TR) && !is_removed(tr)) {
                    *dst = h2m_xrealloc(*dst, (*nd + 1) * sizeof **dst);
                    (*dst)[(*nd)++] = tr;
                }
            }
        }
    }
    size_t total = nh + nb + nf;
    h2m_node **all = h2m_xcalloc(total ? total : 1, sizeof *all);
    size_t k = 0;
    for (size_t i = 0; i < nh; i++)
        all[k++] = head[i];
    for (size_t i = 0; i < nb; i++)
        all[k++] = body[i];
    for (size_t i = 0; i < nf; i++)
        all[k++] = foot[i];
    free(head);
    free(body);
    free(foot);
    *out = all;
    *nout = total;
}

static bool is_cell(const h2m_node *n)
{
    return (h2m_node_is(n, TAG_TD) || h2m_node_is(n, TAG_TH)) && !is_removed(n);
}

static bool is_layout_table(const h2m_node *table, h2m_node **rows, size_t nrows)
{
    const char *role = h2m_node_attr(table, "role");
    if (role && (ascii_ieq(role, "presentation") || ascii_ieq(role, "none")))
        return true;
    size_t cells = 0, widest = 0;
    bool any_th = false;
    for (size_t r = 0; r < nrows; r++) {
        size_t in_row = 0;
        for (h2m_node *ch = rows[r]->first_child; ch; ch = ch->next) {
            if (!is_cell(ch))
                continue;
            cells++;
            in_row++;
            if (in_row > widest)
                widest = in_row;
            if (h2m_node_is(ch, TAG_TH))
                any_th = true;
            if (h2m_node_find(ch, TAG_TABLE))
                return true;
        }
    }
    /* A lone cell, or a single column or row of plain (non-header) cells,
     * is page layout rather than tabular data. */
    bool header = any_th || h2m_node_is(rows[0]->parent, TAG_THEAD);
    return cells <= 1 || (!header && (widest <= 1 || nrows == 1));
}

static void render_layout_table(ctx *c, h2m_sink *s, h2m_node *caption, h2m_node **rows, size_t nrows)
{
    if (caption)
        render_block(c, s, caption);
    for (size_t r = 0; r < nrows; r++) {
        for (h2m_node *ch = rows[r]->first_child; ch; ch = ch->next) {
            if (is_cell(ch))
                render_block(c, s, ch);
        }
    }
}

/* Renders a data table as plain lines ("a | b"), for targets that support
 * neither GFM tables nor raw HTML. */
static void render_text_table(ctx *c, h2m_sink *s, h2m_node *caption, h2m_node **rows, size_t nrows)
{
    if (caption)
        render_block(c, s, caption);
    h2m_buf out = H2M_BUF_INIT;
    for (size_t r = 0; r < nrows; r++) {
        h2m_buf line = H2M_BUF_INIT;
        for (h2m_node *td = rows[r]->first_child; td; td = td->next) {
            if (!is_cell(td))
                continue;
            h2m_sink cs;
            h2m_sink_init(&cs, &c->style, false, 0);
            cs.flatten = true;
            c->flatten++;
            render_children(c, &cs, td);
            c->flatten--;
            h2m_sink_finish(&cs);
            if (line.len)
                h2m_buf_puts(&line, " | ");
            h2m_buf_append(&line, cs.buf.data ? cs.buf.data : "", cs.buf.len);
            h2m_sink_free(&cs);
        }
        if (line.len) {
            if (out.len)
                h2m_buf_puts(&out, c->style.hard_break);
            h2m_buf_append(&out, line.data, line.len);
        }
        h2m_buf_free(&line);
    }
    if (out.len) {
        h2m_sink_block(s, 2);
        h2m_sink_raw_block(s, out.data, out.len);
        h2m_sink_block(s, 2);
    }
    h2m_buf_free(&out);
}

/* Serialises a subtree as minimal, attribute-light HTML (used for tables
 * in CommonMark output). Whitespace is collapsed so the block never
 * contains a blank line. */
static void serialize_html(ctx *c, const h2m_node *root, h2m_buf *out)
{
    static const h2m_tag keep[] = {TAG_TABLE, TAG_CAPTION, TAG_THEAD, TAG_TBODY, TAG_TFOOT, TAG_TR, TAG_TH,
                                   TAG_TD, TAG_A, TAG_IMG, TAG_EM, TAG_I, TAG_STRONG, TAG_B, TAG_CODE,
                                   TAG_BR, TAG_DEL, TAG_S, TAG_SUP, TAG_SUB, TAG_KBD, TAG_UL, TAG_OL, TAG_LI,
                                   TAG_P};
    static const char *const attrs[] = {"href", "src", "alt", "title", "colspan", "rowspan", "align"};
    if (root->type == H2M_NODE_TEXT) {
        bool space = false;
        for (size_t i = 0; i < root->text_len; i++) {
            char ch = root->text[i];
            if (is_html_ws(ch)) {
                space = true;
                continue;
            }
            if (space && out->len && h2m_buf_last(out) != ' ')
                h2m_buf_putc(out, ' ');
            space = false;
            if (ch == '<')
                h2m_buf_puts(out, "&lt;");
            else if (ch == '>')
                h2m_buf_puts(out, "&gt;");
            else if (ch == '&')
                h2m_buf_puts(out, "&amp;");
            else
                h2m_buf_putc(out, ch);
        }
        if (space && out->len && h2m_buf_last(out) != ' ')
            h2m_buf_putc(out, ' ');
        return;
    }
    if (!is_element(root) || is_dropped(root))
        return;
    bool kept = false;
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) {
        if (h2m_node_is(root, keep[i]))
            kept = true;
    }
    if (kept) {
        h2m_buf_printf(out, "<%s", root->name);
        for (size_t i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
            const char *v = h2m_node_attr(root, attrs[i]);
            if (!v)
                continue;
            h2m_buf val = H2M_BUF_INIT;
            if (strcmp(attrs[i], "href") == 0 || strcmp(attrs[i], "src") == 0)
                resolve_url(c, v, &val);
            else
                h2m_buf_puts(&val, v);
            h2m_buf_printf(out, " %s=\"", attrs[i]);
            for (size_t k = 0; k < val.len; k++) {
                char ch = val.data[k];
                if (ch == '"')
                    h2m_buf_puts(out, "&quot;");
                else if (ch == '&')
                    h2m_buf_puts(out, "&amp;");
                else if (ch == '<')
                    h2m_buf_puts(out, "&lt;");
                else if (ch == '\n')
                    h2m_buf_putc(out, ' ');
                else
                    h2m_buf_putc(out, ch);
            }
            h2m_buf_putc(out, '"');
            h2m_buf_free(&val);
        }
        h2m_buf_putc(out, '>');
    }
    if (!h2m_node_is(root, TAG_BR) && !h2m_node_is(root, TAG_IMG)) {
        for (const h2m_node *ch = root->first_child; ch; ch = ch->next)
            serialize_html(c, ch, out);
        if (kept)
            h2m_buf_printf(out, "</%s>", root->name);
    }
}

static void render_table(ctx *c, h2m_sink *s, h2m_node *table)
{
    if (c->flatten || s->flatten) {
        render_block(c, s, table);
        return;
    }
    h2m_node **rows;
    size_t nrows;
    h2m_node *caption;
    collect_rows(table, &rows, &nrows, &caption);

    if (nrows == 0) {
        if (caption)
            render_block(c, s, caption);
        free(rows);
        return;
    }
    if (is_layout_table(table, rows, nrows)) {
        render_layout_table(c, s, caption, rows, nrows);
        free(rows);
        return;
    }
    if (!c->gfm) {
        if (c->raw_html) {
            h2m_buf html = H2M_BUF_INIT;
            serialize_html(c, table, &html);
            h2m_sink_block(s, 2);
            h2m_sink_raw_block(s, html.data, html.len);
            h2m_sink_block(s, 2);
            h2m_buf_free(&html);
        } else {
            render_text_table(c, s, caption, rows, nrows);
        }
        free(rows);
        return;
    }

    row *grid = h2m_xcalloc(nrows, sizeof *grid);
    size_t ncols = 0, total_cells = 0;
    bool too_big = false;
    size_t *span_left = NULL; /* remaining rowspan per column */
    size_t span_cap = 0;
    bool any_content = false;

    for (size_t r = 0; r < nrows && !too_big; r++) {
        row *rw = &grid[r];
        h2m_node *tr = rows[r];
        bool all_th = true, any_cell = false;
        h2m_node *parent = tr->parent;
        size_t col = 0;
        for (h2m_node *td = tr->first_child; td; td = td->next) {
            if (!is_cell(td))
                continue;
            /* Skip columns occupied by a rowspan from an earlier row. */
            while (col < span_cap && span_left[col] > 0) {
                span_left[col]--;
                row_add(rw, h2m_xstrdup(""), 0, 0);
                col++;
            }
            any_cell = true;
            if (!h2m_node_is(td, TAG_TH))
                all_th = false;

            h2m_sink cs;
            h2m_sink_init(&cs, &c->style, false, 0);
            cs.flatten = true;
            cs.flatten_sep = c->cell_sep;
            c->flatten++;
            render_children(c, &cs, td);
            c->flatten--;
            h2m_sink_finish(&cs);
            h2m_buf esc = H2M_BUF_INIT;
            for (size_t i = 0; i < cs.buf.len; i++) {
                char ch = cs.buf.data[i];
                if (ch == '|')
                    h2m_buf_putc(&esc, '\\');
                if (ch == '\n')
                    ch = ' ';
                h2m_buf_putc(&esc, ch);
            }
            h2m_sink_free(&cs);
            if (esc.len)
                any_content = true;
            size_t len = esc.len;
            row_add(rw, h2m_buf_detach(&esc, NULL), len, cell_align(td));

            long colspan = parse_long(h2m_node_attr(td, "colspan"), 1);
            long rowspan = parse_long(h2m_node_attr(td, "rowspan"), 1);
            if (colspan < 1)
                colspan = 1;
            if (colspan > 64)
                colspan = 64;
            if ((size_t)rowspan > nrows - r)
                rowspan = (long)(nrows - r); /* spans never extend past the table */
            for (long k = 0; k < colspan; k++) {
                size_t cc = col + (size_t)k;
                if (cc >= span_cap) {
                    size_t ncap = cc + 16;
                    span_left = h2m_xrealloc(span_left, ncap * sizeof *span_left);
                    memset(span_left + span_cap, 0, (ncap - span_cap) * sizeof *span_left);
                    span_cap = ncap;
                }
                span_left[cc] = rowspan > 1 ? (size_t)(rowspan - 1) : 0;
                if (k > 0)
                    row_add(rw, h2m_xstrdup(""), 0, 0);
            }
            col += (size_t)colspan;
            if (col > MAX_TABLE_COLUMNS) {
                too_big = true;
                break;
            }
        }
        while (col < span_cap && span_left[col] > 0) {
            span_left[col]--;
            row_add(rw, h2m_xstrdup(""), 0, 0);
            col++;
        }
        rw->header = any_cell && (all_th || h2m_node_is(parent, TAG_THEAD));
        if (rw->n > ncols)
            ncols = rw->n;
        total_cells += rw->n;
        if (total_cells > MAX_TABLE_CELLS)
            too_big = true;
    }
    free(span_left);

    if (too_big) {
        /* Degenerate geometry (e.g. chained rowspans): keep the content,
         * drop the grid. */
        for (size_t r = 0; r < nrows; r++) {
            for (size_t k = 0; k < grid[r].n; k++)
                free(grid[r].cells[k].text);
            free(grid[r].cells);
        }
        free(grid);
        render_layout_table(c, s, caption, rows, nrows);
        free(rows);
        return;
    }

    if (ncols == 0 || !any_content) {
        if (caption)
            render_block(c, s, caption);
        for (size_t r = 0; r < nrows; r++) {
            for (size_t k = 0; k < grid[r].n; k++)
                free(grid[r].cells[k].text);
            free(grid[r].cells);
        }
        free(grid);
        free(rows);
        return;
    }

    /* Drop rows that have no cells at all (e.g. empty <tr></tr>). */
    size_t first = 0;
    while (first < nrows && grid[first].n == 0)
        first++;

    /* Drop columns that are empty in every row (spacer or icon columns). */
    for (size_t col = ncols; col-- > 0;) {
        bool empty = true;
        for (size_t r = first; r < nrows && empty; r++) {
            if (col < grid[r].n && grid[r].cells[col].len)
                empty = false;
        }
        if (!empty)
            continue;
        for (size_t r = first; r < nrows; r++) {
            if (col >= grid[r].n)
                continue;
            free(grid[r].cells[col].text);
            memmove(&grid[r].cells[col], &grid[r].cells[col + 1], (grid[r].n - col - 1) * sizeof(cell));
            grid[r].n--;
        }
        ncols--;
    }

    int *align = h2m_xcalloc(ncols, sizeof *align);
    size_t *width = h2m_xcalloc(ncols, sizeof *width);
    for (size_t col = 0; col < ncols; col++) {
        width[col] = 3;
        for (size_t r = first; r < nrows; r++) {
            if (col < grid[r].n) {
                size_t w = h2m_utf8_width(grid[r].cells[col].text, grid[r].cells[col].len);
                if (w > width[col])
                    width[col] = w;
                if (!align[col] && grid[r].cells[col].align)
                    align[col] = grid[r].cells[col].align;
            }
        }
    }

    h2m_buf out = H2M_BUF_INIT;
    bool pad = !c->opt->compact_tables;
    bool wrote_delim = false;
    for (size_t r = first; r < nrows; r++) {
        if (grid[r].n == 0)
            continue;
        if (out.len)
            h2m_buf_putc(&out, '\n');
        h2m_buf_putc(&out, '|');
        for (size_t col = 0; col < ncols; col++) {
            const char *t = col < grid[r].n ? grid[r].cells[col].text : "";
            size_t tl = col < grid[r].n ? grid[r].cells[col].len : 0;
            size_t fill = pad ? width[col] - h2m_utf8_width(t, tl) : 0;
            size_t before = align[col] == 3 ? fill : align[col] == 2 ? fill / 2 : 0;
            h2m_buf_putc(&out, ' ');
            h2m_buf_repeat(&out, ' ', before);
            h2m_buf_append(&out, t, tl);
            h2m_buf_repeat(&out, ' ', fill - before);
            h2m_buf_puts(&out, " |");
        }
        if (!wrote_delim) {
            wrote_delim = true;
            h2m_buf_puts(&out, "\n|");
            for (size_t col = 0; col < ncols; col++) {
                size_t w = pad ? width[col] : 3;
                h2m_buf_putc(&out, ' ');
                switch (align[col]) {
                case 1:
                    h2m_buf_putc(&out, ':');
                    h2m_buf_repeat(&out, '-', w - 1);
                    break;
                case 2:
                    h2m_buf_putc(&out, ':');
                    h2m_buf_repeat(&out, '-', w - 2);
                    h2m_buf_putc(&out, ':');
                    break;
                case 3:
                    h2m_buf_repeat(&out, '-', w - 1);
                    h2m_buf_putc(&out, ':');
                    break;
                default:
                    h2m_buf_repeat(&out, '-', w);
                    break;
                }
                h2m_buf_puts(&out, " |");
            }
        }
    }

    if (caption)
        render_block(c, s, caption);
    h2m_sink_block(s, 2);
    h2m_sink_raw_block(s, out.data, out.len);
    h2m_sink_block(s, 2);

    h2m_buf_free(&out);
    free(align);
    free(width);
    for (size_t r = 0; r < nrows; r++) {
        for (size_t k = 0; k < grid[r].n; k++)
            free(grid[r].cells[k].text);
        free(grid[r].cells);
    }
    free(grid);
    free(rows);
}

/* ------------------------------------------------------------------------ */
/* Dispatch                                                                 */
/* ------------------------------------------------------------------------ */

static void render_children(ctx *c, h2m_sink *s, h2m_node *n)
{
    for (h2m_node *ch = n->first_child; ch; ch = ch->next)
        render_node(c, s, ch);
}

static void render_quote(ctx *c, h2m_sink *s, h2m_node *n)
{
    h2m_sink child;
    child_inline(c, s, &child);
    render_children(c, &child, n);
    h2m_sink_finish(&child);
    emit_between(s, &child, "\"", "\"");
    h2m_sink_free(&child);
}

static void render_rt(ctx *c, h2m_sink *s, h2m_node *n)
{
    /* Without <rp> fallback parentheses, show the annotation in brackets. */
    const h2m_node *ruby = n->parent;
    bool has_rp = false;
    if (ruby) {
        for (const h2m_node *ch = ruby->first_child; ch; ch = ch->next) {
            if (h2m_node_is(ch, TAG_RP))
                has_rp = true;
        }
    }
    if (has_rp) {
        render_children(c, s, n);
        return;
    }
    h2m_sink child;
    child_inline(c, s, &child);
    render_children(c, &child, n);
    h2m_sink_finish(&child);
    emit_between(s, &child, "(", ")");
    h2m_sink_free(&child);
}

static void render_dt(ctx *c, h2m_sink *s, h2m_node *n)
{
    h2m_sink_block(s, 2);
    render_emphasis(c, s, n, EMPH_STRONG);
    h2m_sink_block(s, 2);
}

static void render_element(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (n->ns == H2M_NS_SVG)
        return;
    if (n->ns == H2M_NS_MATHML) {
        if (strcmp(n->name, "math") == 0)
            render_math(c, s, n);
        else
            render_children(c, s, n);
        return;
    }

    switch (n->tag) {
    case TAG_SCRIPT: {
        const char *type = h2m_node_attr(n, "type");
        if (type && ascii_istarts(type, "math/tex")) {
            h2m_buf tex = H2M_BUF_INIT;
            collect_text(n, &tex, NULL);
            emit_tex(c, s, tex.data, tex.len, strstr(type, "mode=display") != NULL);
            h2m_buf_free(&tex);
        }
        return;
    }
    case TAG_P:
    case TAG_ADDRESS: case TAG_ARTICLE: case TAG_ASIDE: case TAG_CENTER: case TAG_DD:
    case TAG_DETAILS: case TAG_DIALOG: case TAG_DIV: case TAG_DL: case TAG_FIELDSET:
    case TAG_FIGCAPTION: case TAG_FIGURE: case TAG_FOOTER: case TAG_FORM: case TAG_HEADER:
    case TAG_HGROUP: case TAG_LEGEND: case TAG_MAIN: case TAG_MARQUEE: case TAG_NAV:
    case TAG_SEARCH: case TAG_SECTION: case TAG_CAPTION: case TAG_TBODY: case TAG_THEAD:
    case TAG_TFOOT: case TAG_TR: case TAG_TD: case TAG_TH: case TAG_LI: case TAG_BODY:
    case TAG_HTML:
        render_block(c, s, n);
        return;
    case TAG_SUMMARY:
        render_dt(c, s, n);
        return;
    case TAG_DT:
        render_dt(c, s, n);
        return;
    case TAG_H1: case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6:
        render_heading(c, s, n);
        return;
    case TAG_BLOCKQUOTE:
        render_blockquote(c, s, n);
        return;
    case TAG_UL: case TAG_OL: case TAG_MENU: case TAG_DIR:
        render_list(c, s, n);
        return;
    case TAG_PRE: case TAG_LISTING: case TAG_XMP: case TAG_PLAINTEXT:
        render_pre(c, s, n);
        return;
    case TAG_HR:
        render_hr(c, s);
        return;
    case TAG_TABLE:
        render_table(c, s, n);
        return;
    case TAG_BR:
        h2m_sink_br(s);
        return;
    case TAG_WBR:
        return;
    case TAG_IMG:
        render_image(c, s, n);
        return;
    case TAG_A:
        render_link(c, s, n);
        return;
    case TAG_EM: case TAG_I: case TAG_CITE: case TAG_DFN: case TAG_VAR:
        render_emphasis(c, s, n, EMPH_EM);
        return;
    case TAG_STRONG: case TAG_B: {
        char v[32];
        if (style_value(n, "font-weight", v, sizeof v) &&
            (strcmp(v, "normal") == 0 || strcmp(v, "400") == 0 || strcmp(v, "lighter") == 0 ||
             strcmp(v, "300") == 0)) {
            render_emphasis(c, s, n, span_style_kinds(n) & ~(unsigned)EMPH_STRONG);
            return;
        }
        render_emphasis(c, s, n, EMPH_STRONG | span_style_kinds(n));
        return;
    }
    case TAG_DEL: case TAG_S: case TAG_STRIKE:
        render_emphasis(c, s, n, EMPH_STRIKE);
        return;
    case TAG_CODE: case TAG_TT: case TAG_SAMP:
        render_code(c, s, n);
        return;
    case TAG_KBD:
        if (c->raw_html)
            render_html_inline(c, s, n, "kbd");
        else
            render_code(c, s, n);
        return;
    case TAG_SUP:
        render_html_inline(c, s, n, "sup");
        return;
    case TAG_SUB:
        render_html_inline(c, s, n, "sub");
        return;
    case TAG_Q:
        render_quote(c, s, n);
        return;
    case TAG_INPUT:
        render_input(c, s, n);
        return;
    case TAG_AUDIO:
    case TAG_VIDEO:
        render_media(c, s, n);
        return;
    case TAG_RT:
        render_rt(c, s, n);
        return;
    case TAG_SPAN:
        render_emphasis(c, s, n, span_style_kinds(n));
        return;
    case TAG_BUTTON: {
        /* A button wrapping a heading is a disclosure/accordion header
         * (the heading is document structure, not a control label). */
        for (const h2m_node *d = n->first_child; d; d = h2m_node_next_in_tree(d, n, false)) {
            if (is_element(d) && d->ns == H2M_NS_HTML && h2m_tag_is_heading(d->tag) && !is_dropped(d)) {
                render_block(c, s, n);
                return;
            }
        }
        return;
    }
    default:
        if (h2m_tag_has(n->tag, TF_DROP))
            return;
        render_children(c, s, n);
        return;
    }
}

static void render_node(ctx *c, h2m_sink *s, h2m_node *n)
{
    if (n->type == H2M_NODE_TEXT) {
        h2m_sink_text(s, n->text, n->text_len);
        return;
    }
    if (n->type == H2M_NODE_DOCUMENT) {
        render_children(c, s, n);
        return;
    }
    if (is_removed(n))
        return;
    if (c->render_depth >= MAX_RENDER_DEPTH) {
        /* Pathologically deep trees are flattened to text rather than
         * risking stack exhaustion. */
        h2m_buf text = H2M_BUF_INIT;
        collect_text(n, &text, " ");
        h2m_sink_text(s, text.data, text.len);
        h2m_buf_free(&text);
        return;
    }
    c->render_depth++;
    render_element(c, s, n);
    c->render_depth--;
}

/* ------------------------------------------------------------------------ */
/* Document-level processing                                                */
/* ------------------------------------------------------------------------ */

static h2m_node *find_body(h2m_document *doc)
{
    h2m_node *body = h2m_node_find(doc->root, TAG_BODY);
    return body ? body : doc->root;
}

static bool within(const h2m_node *n, h2m_tag tag, const h2m_node *stop)
{
    for (const h2m_node *p = n->parent; p && p != stop; p = p->parent) {
        if (h2m_node_is(p, tag))
            return true;
    }
    return false;
}

static const char *role_of(const h2m_node *n)
{
    const char *r = h2m_node_attr(n, "role");
    return r ? r : "";
}

/* Amount of non-whitespace text in a subtree, ignoring dropped content. */
static size_t visible_text_length(const h2m_node *root)
{
    size_t n = 0;
    for (const h2m_node *c = root->first_child; c;) {
        bool skip = false;
        if (c->type == H2M_NODE_TEXT) {
            for (size_t i = 0; i < c->text_len; i++)
                n += !is_html_ws(c->text[i]);
        } else if (is_element(c) && (is_removed(c) || is_dropped(c))) {
            skip = true;
        }
        c = h2m_node_next_in_tree(c, root, skip);
    }
    return n;
}

/* Chooses the main content root and marks site chrome as removed. */
static h2m_node *main_content_root(h2m_document *doc)
{
    h2m_node *body = find_body(doc);
    h2m_node *main_el = NULL, *role_main = NULL, *first_article = NULL;
    size_t articles = 0;
    for (h2m_node *n = body->first_child; n;) {
        bool skip = false;
        if (is_element(n)) {
            if (is_removed(n) || is_dropped(n)) {
                skip = true;
            } else {
                if (!main_el && h2m_node_is(n, TAG_MAIN))
                    main_el = n;
                if (!role_main && ascii_ieq(role_of(n), "main"))
                    role_main = n;
                if (h2m_node_is(n, TAG_ARTICLE) && !within(n, TAG_ARTICLE, body)) {
                    if (!first_article)
                        first_article = n;
                    articles++;
                }
            }
        }
        n = h2m_node_next_in_tree(n, body, skip);
    }
    h2m_node *root = main_el ? main_el : role_main ? role_main : articles == 1 ? first_article : body;

    /* A <main> wrapping application chrome around one dominant <article>
     * (a README inside a repository page, a post inside a feed layout):
     * prefer the article, unless the page title lives outside it. */
    if (root != body && root != first_article) {
        h2m_node *only = NULL;
        size_t count = 0;
        bool h1_outside = false;
        for (h2m_node *n = root->first_child; n;) {
            bool skip = false;
            if (is_element(n) && (is_removed(n) || is_dropped(n))) {
                skip = true;
            } else if (h2m_node_is(n, TAG_ARTICLE)) {
                only = n;
                count++;
                skip = true;
            } else if (h2m_node_is(n, TAG_H1)) {
                h1_outside = true;
            }
            n = h2m_node_next_in_tree(n, root, skip);
        }
        if (count == 1 && !h1_outside) {
            size_t whole = visible_text_length(root), part = visible_text_length(only);
            if (whole && part * 10 >= whole * 6)
                root = only;
        }
    }

    for (h2m_node *n = root->first_child; n;) {
        bool skip = false;
        if (is_element(n) && !is_removed(n)) {
            const char *role = role_of(n);
            bool in_content = within(n, TAG_ARTICLE, NULL) || within(n, TAG_MAIN, NULL) ||
                              root != body;
            bool chrome = h2m_node_is(n, TAG_NAV) || ascii_ieq(role, "navigation") || ascii_ieq(role, "search") ||
                          h2m_node_is(n, TAG_SEARCH);
            if (!in_content)
                chrome = chrome || h2m_node_is(n, TAG_HEADER) || h2m_node_is(n, TAG_FOOTER) ||
                         h2m_node_is(n, TAG_ASIDE) || ascii_ieq(role, "banner") ||
                         ascii_ieq(role, "contentinfo") || ascii_ieq(role, "complementary");
            if (chrome) {
                n->flags |= H2M_NODE_F_REMOVED;
                skip = true;
            }
        }
        n = h2m_node_next_in_tree(n, root, skip);
    }
    return root;
}

static bool contains_mathml(const h2m_node *root)
{
    for (const h2m_node *n = root->first_child; n; n = h2m_node_next_in_tree(n, root, false)) {
        if (is_element(n) && n->ns == H2M_NS_MATHML)
            return true;
    }
    return false;
}

/* Marks hidden and excluded elements. */
static void mark_removed(h2m_document *doc, const h2m_convert_input *in)
{
    for (h2m_node *n = doc->root->first_child; n;) {
        bool skip = false;
        if (is_element(n)) {
            /* MathML hidden from sighted users (next to an image or HTML
             * rendering) is the only machine-readable form of an equation. */
            bool hidden = !in->opts->keep_hidden && is_hidden(n) &&
                          !(h2m_node_attr(n, "aria-hidden") == NULL && contains_mathml(n));
            if (hidden || (in->exclude && h2m_selector_match(in->exclude, n))) {
                n->flags |= H2M_NODE_F_REMOVED;
                skip = true;
            }
        }
        n = h2m_node_next_in_tree(n, doc->root, skip);
    }
}

static const char *meta_content(h2m_document *doc, const char *key, const char *value)
{
    for (h2m_node *n = doc->root->first_child; n; n = h2m_node_next_in_tree(n, doc->root, false)) {
        if (!h2m_node_is(n, TAG_META))
            continue;
        const char *k = h2m_node_attr(n, key);
        if (k && ascii_ieq(k, value)) {
            const char *content = h2m_node_attr(n, "content");
            if (content && *content)
                return content;
        }
    }
    return NULL;
}

static void yaml_field(h2m_buf *out, const char *key, const char *value)
{
    if (!value)
        return;
    h2m_buf clean = H2M_BUF_INIT;
    append_collapsed(&clean, value, strlen(value));
    if (clean.len) {
        h2m_buf_printf(out, "%s: \"", key);
        for (size_t i = 0; i < clean.len; i++) {
            unsigned char ch = (unsigned char)clean.data[i];
            if (ch == '"' || ch == '\\')
                h2m_buf_putc(out, '\\');
            if (ch < 0x20)
                continue;
            h2m_buf_putc(out, (char)ch);
        }
        h2m_buf_puts(out, "\"\n");
    }
    h2m_buf_free(&clean);
}

static void front_matter(ctx *c, h2m_document *doc, h2m_buf *out)
{
    h2m_buf title = H2M_BUF_INIT;
    for (h2m_node *n = doc->root->first_child; n; n = h2m_node_next_in_tree(n, doc->root, false)) {
        if (h2m_node_is(n, TAG_TITLE)) {
            collect_text_collapsed(n, &title);
            break;
        }
    }
    const char *t = title.len ? title.data : meta_content(doc, "property", "og:title");
    const char *desc = meta_content(doc, "name", "description");
    if (!desc)
        desc = meta_content(doc, "property", "og:description");
    const char *author = meta_content(doc, "name", "author");
    const char *date = meta_content(doc, "property", "article:published_time");
    if (!date)
        date = meta_content(doc, "name", "date");
    const char *lang = NULL;
    h2m_node *html = h2m_node_find(doc->root, TAG_HTML);
    if (html)
        lang = h2m_node_attr(html, "lang");

    h2m_buf url = H2M_BUF_INIT;
    for (h2m_node *n = doc->root->first_child; n; n = h2m_node_next_in_tree(n, doc->root, false)) {
        const char *rel = h2m_node_attr(n, "rel");
        if (h2m_node_is(n, TAG_LINK) && rel && ascii_ieq(rel, "canonical") && h2m_node_attr(n, "href")) {
            resolve_url(c, h2m_node_attr(n, "href"), &url);
            break;
        }
    }
    if (!url.len && c->opt->base_url)
        h2m_url_clean(c->opt->base_url, &url);

    h2m_buf_puts(out, "---\n");
    yaml_field(out, "title", t);
    yaml_field(out, "description", desc);
    yaml_field(out, "author", author);
    yaml_field(out, "date", date);
    yaml_field(out, "lang", lang);
    yaml_field(out, "url", url.len ? url.data : NULL);
    h2m_buf_puts(out, "---\n");
    h2m_buf_free(&title);
    h2m_buf_free(&url);
}

/* Form controls whose text is dropped as chrome rather than as noise. */
static bool is_form_control(const h2m_node *n)
{
    switch (n->tag) {
    case TAG_BUTTON: case TAG_SELECT: case TAG_TEXTAREA: case TAG_OPTION: case TAG_OPTGROUP:
    case TAG_DATALIST: case TAG_OUTPUT: case TAG_METER: case TAG_PROGRESS:
        return n->ns == H2M_NS_HTML;
    default:
        return false;
    }
}

/*
 * When little or no Markdown comes out of a document that clearly had text,
 * explain which rule removed it and how to get it back. Without this the
 * user just gets an empty file.
 */
static void diagnose_empty_output(ctx *c, h2m_document *doc, const h2m_convert_input *in, size_t kept)
{
    if (!c->opt->warning)
        return;
    h2m_node *body = find_body(doc);
    size_t hidden = 0, excluded = 0, controls = 0, unselected = 0;

    for (h2m_node *n = body->first_child; n;) {
        bool skip = false;
        if (is_element(n)) {
            if (is_removed(n)) {
                size_t len = visible_text_length(n);
                if (!c->opt->keep_hidden && is_hidden(n))
                    hidden += len;
                else if (in->exclude && h2m_selector_match(in->exclude, n))
                    excluded += len;
                else
                    unselected += len; /* site chrome dropped by --main */
                skip = true;
            } else if (is_form_control(n)) {
                controls += visible_text_length(n);
                skip = true;
            }
        }
        n = h2m_node_next_in_tree(n, body, skip);
    }
    if (in->select) {
        size_t total = visible_text_length(body);
        size_t accounted = hidden + excluded + controls;
        unselected = total > accounted + kept ? total - accounted - kept : 0;
    }

    size_t dropped = hidden + excluded + controls + unselected;
    if (dropped < 1000 || kept > dropped / 10)
        return;

    const char *what;
    const char *hint;
    size_t amount;
    if (hidden >= excluded && hidden >= controls && hidden >= unselected) {
        amount = hidden;
        what = "inside elements that are hidden (hidden, aria-hidden or display:none)";
        hint = "use --keep-hidden to convert it";
    } else if (unselected >= excluded && unselected >= controls) {
        amount = unselected;
        what = in->select ? "outside the elements matched by --select" : "outside the main content";
        hint = in->select ? "check the --select selector" : "try again without --main";
    } else if (excluded >= controls) {
        amount = excluded;
        what = "inside elements removed by --exclude";
        hint = "check the --exclude selector";
    } else {
        amount = controls;
        what = "inside form controls, which are not converted";
        hint = "select the surrounding content with --select";
    }

    char msg[256];
    snprintf(msg, sizeof msg, "%s; %zu characters of text are %s - %s",
             kept ? "almost no Markdown was produced" : "the output is empty", amount, what, hint);
    c->opt->warning(c->opt->warning_data, msg);
}

void h2m_convert_document(h2m_document *doc, const h2m_convert_input *in, h2m_buf *out)
{
    const h2m_options *o = in->opts;
    ctx c;
    memset(&c, 0, sizeof c);
    c.opt = o;
    c.gfm = o->flavor == H2M_FLAVOR_GFM;
    c.raw_html = o->raw_html;
    c.style.hard_break = o->br_style == H2M_BR_SPACES ? "  \n" : "\\\n";
    c.style.gfm = c.gfm;
    c.style.nbsp_is_space = !o->preserve_nbsp;
    c.cell_sep = c.raw_html ? "<br>" : NULL;

    /* Emphasis delimiter candidates, preferred first. */
    char em = o->emphasis == '_' ? '_' : '*';
    char alt_em = em == '*' ? '_' : '*';
    c.em[c.n_em++] = (h2m_delim){em == '*' ? "*" : "_", em == '*' ? "*" : "_", em};
    c.em[c.n_em++] = (h2m_delim){alt_em == '*' ? "*" : "_", alt_em == '*' ? "*" : "_", alt_em};
    if (c.raw_html)
        c.em[c.n_em++] = (h2m_delim){"<em>", "</em>", 0};
    char st = o->strong == '_' ? '_' : '*';
    char alt_st = st == '*' ? '_' : '*';
    c.strong[c.n_strong++] = (h2m_delim){st == '*' ? "**" : "__", st == '*' ? "**" : "__", st};
    c.strong[c.n_strong++] = (h2m_delim){alt_st == '*' ? "**" : "__", alt_st == '*' ? "**" : "__", alt_st};
    if (c.raw_html)
        c.strong[c.n_strong++] = (h2m_delim){"<strong>", "</strong>", 0};
    if (c.gfm)
        c.strike[c.n_strike++] = (h2m_delim){"~~", "~~", '~'};
    if (c.raw_html)
        c.strike[c.n_strike++] = (h2m_delim){"<del>", "</del>", 0};

    /* Effective base URL: the option, refined by <base href>. */
    h2m_buf base = H2M_BUF_INIT;
    h2m_node *base_el = NULL;
    for (h2m_node *n = doc->root->first_child; n; n = h2m_node_next_in_tree(n, doc->root, false)) {
        if (h2m_node_is(n, TAG_BASE) && h2m_node_attr(n, "href")) {
            base_el = n;
            break;
        }
    }
    if (base_el) {
        h2m_buf clean = H2M_BUF_INIT;
        h2m_url_clean(h2m_node_attr(base_el, "href"), &clean);
        h2m_url_resolve(o->base_url, clean.data, &base);
        h2m_buf_free(&clean);
    } else if (o->base_url) {
        h2m_url_clean(o->base_url, &base);
    }
    c.base = (base.len && h2m_url_has_scheme(base.data)) ? base.data : NULL;

    mark_removed(doc, in);

    h2m_sink top;
    h2m_sink_init(&top, &c.style, true, o->wrap);

    if (in->select) {
        for (h2m_node *n = doc->root->first_child; n;) {
            bool skip = false;
            if (is_element(n)) {
                if (is_removed(n)) {
                    skip = true;
                } else if (h2m_selector_match(in->select, n)) {
                    h2m_sink_block(&top, 2);
                    render_node(&c, &top, n);
                    h2m_sink_block(&top, 2);
                    skip = true;
                }
            }
            n = h2m_node_next_in_tree(n, doc->root, skip);
        }
    } else if (o->main_content) {
        render_node(&c, &top, main_content_root(doc));
    } else {
        render_node(&c, &top, find_body(doc));
    }
    h2m_sink_finish(&top);

    size_t kept = 0;
    for (size_t i = 0; i < top.buf.len; i++)
        kept += !is_html_ws(top.buf.data[i]);
    diagnose_empty_output(&c, doc, in, kept);

    if (o->front_matter)
        front_matter(&c, doc, out);
    if (top.buf.len) {
        if (o->front_matter)
            h2m_buf_putc(out, '\n');
        h2m_buf_append(out, top.buf.data, top.buf.len);
        h2m_buf_putc(out, '\n');
    }
    if (c.nrefs) {
        if (out->len)
            h2m_buf_putc(out, '\n');
        for (size_t i = 0; i < c.nrefs; i++) {
            h2m_buf_printf(out, "[%zu]: ", i + 1);
            append_destination(out, c.refs[i].url);
            if (c.refs[i].title)
                append_title(out, c.refs[i].title);
            h2m_buf_putc(out, '\n');
        }
    }
    if (!out->data)
        h2m_buf_append(out, "", 0);

    for (size_t i = 0; i < c.nrefs; i++) {
        free(c.refs[i].url);
        free(c.refs[i].title);
    }
    free(c.refs);
    h2m_sink_free(&top);
    h2m_buf_free(&base);
}
