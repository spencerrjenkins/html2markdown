/*
 * HTML tokenizer and tree builder.
 *
 * The tokenizer implements the WHATWG tokenization states that matter for
 * content extraction (data, tag, attribute, comment, DOCTYPE, bogus comment,
 * RCDATA, RAWTEXT, PLAINTEXT and CDATA in foreign content). Script data is
 * treated as RAWTEXT, which differs from browsers only for pathological
 * "<!--<script>" sequences inside scripts - and scripts are discarded anyway.
 *
 * The tree builder follows the "in body" insertion mode closely, including
 * the list of active formatting elements, the adoption agency algorithm,
 * scope checks and foster parenting, with simplified handling of the
 * document-level modes (initial, before head, in head, after head).
 *
 * SPDX-License-Identifier: MIT
 */
#include "parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "entities.h"

/* ------------------------------------------------------------------------ */
/* Tokenizer                                                                */
/* ------------------------------------------------------------------------ */

typedef enum {
    TOK_EOF,
    TOK_TEXT,
    TOK_START,
    TOK_END
} tok_type;

typedef enum {
    MODE_DATA,
    MODE_RCDATA,
    MODE_RAWTEXT,
    MODE_PLAINTEXT
} tok_mode;

typedef struct {
    h2m_buf name;
    h2m_buf value;
} tok_attr;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;

    tok_mode mode;
    char end_tag[16]; /* appropriate end tag name for RCDATA/RAWTEXT */
    size_t end_tag_len;
    bool cdata_allowed; /* current node is in foreign content */

    tok_type type;
    h2m_buf text;
    h2m_buf tag;
    tok_attr *attrs;
    size_t nattrs;
    size_t cap_attrs;
    bool self_closing;
} tokenizer;

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static void tok_init(tokenizer *t, const char *src, size_t len)
{
    memset(t, 0, sizeof *t);
    t->src = src;
    t->len = len;
}

static void tok_free(tokenizer *t)
{
    h2m_buf_free(&t->text);
    h2m_buf_free(&t->tag);
    for (size_t i = 0; i < t->cap_attrs; i++) {
        h2m_buf_free(&t->attrs[i].name);
        h2m_buf_free(&t->attrs[i].value);
    }
    free(t->attrs);
}

static tok_attr *tok_new_attr(tokenizer *t)
{
    if (t->nattrs == t->cap_attrs) {
        size_t cap = t->cap_attrs ? t->cap_attrs * 2 : 8;
        t->attrs = h2m_xrealloc(t->attrs, cap * sizeof *t->attrs);
        for (size_t i = t->cap_attrs; i < cap; i++) {
            h2m_buf_init(&t->attrs[i].name);
            h2m_buf_init(&t->attrs[i].value);
        }
        t->cap_attrs = cap;
    }
    tok_attr *a = &t->attrs[t->nattrs++];
    h2m_buf_clear(&a->name);
    h2m_buf_clear(&a->value);
    return a;
}

#define MAX_ATTRS 256 /* attributes kept per element */

/* Looks for an attribute among the first `count` attributes of the token. */
static bool tok_has_attr(const tokenizer *t, size_t count, const char *name)
{
    size_t len = strlen(name);
    for (size_t i = 0; i < count; i++) {
        if (t->attrs[i].name.len == len && memcmp(t->attrs[i].name.data, name, len) == 0)
            return true;
    }
    return false;
}

/* Skips a comment starting at "<!--". */
static void skip_comment(tokenizer *t)
{
    size_t p = t->pos + 4;
    if (p < t->len && t->src[p] == '>') {
        t->pos = p + 1;
        return;
    }
    if (p + 1 < t->len && t->src[p] == '-' && t->src[p + 1] == '>') {
        t->pos = p + 2;
        return;
    }
    while (p < t->len) {
        const char *dash = memchr(t->src + p, '-', t->len - p);
        if (!dash)
            break;
        p = (size_t)(dash - t->src);
        if (p + 2 < t->len && t->src[p + 1] == '-' && t->src[p + 2] == '>') {
            t->pos = p + 3;
            return;
        }
        if (p + 3 < t->len && t->src[p + 1] == '-' && t->src[p + 2] == '!' && t->src[p + 3] == '>') {
            t->pos = p + 4;
            return;
        }
        p++;
    }
    t->pos = t->len;
}

/* Skips a DOCTYPE, processing instruction or other bogus comment. */
static void skip_to_gt(tokenizer *t)
{
    const char *gt = memchr(t->src + t->pos, '>', t->len - t->pos);
    t->pos = gt ? (size_t)(gt - t->src) + 1 : t->len;
}

static bool starts_with_ci(const char *s, size_t len, const char *prefix)
{
    size_t n = strlen(prefix);
    if (len < n)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (lower(s[i]) != prefix[i])
            return false;
    }
    return true;
}

/* Reads an attribute value up to (not including) the terminator. */
static void read_attr_value(tokenizer *t, h2m_buf *value, size_t start, size_t end)
{
    h2m_decode_entities(t->src + start, end - start, true, value);
}

/*
 * Parses a start or end tag. t->pos points just past '<' (or "</").
 * Returns false when EOF was reached inside the tag (token discarded).
 */
static bool parse_tag(tokenizer *t, bool end_tag)
{
    const char *s = t->src;
    size_t n = t->len;
    size_t p = t->pos;

    h2m_buf_clear(&t->tag);
    t->nattrs = 0;
    t->self_closing = false;

    while (p < n && !is_ws(s[p]) && s[p] != '/' && s[p] != '>') {
        char c = lower(s[p]);
        if (c == '\0') {
            h2m_buf_puts(&t->tag, "\xEF\xBF\xBD");
        } else {
            h2m_buf_putc(&t->tag, c);
        }
        p++;
    }

    for (;;) {
        while (p < n && is_ws(s[p]))
            p++;
        if (p >= n) {
            t->pos = n;
            return false;
        }
        if (s[p] == '>') {
            p++;
            break;
        }
        if (s[p] == '/') {
            p++;
            if (p < n && s[p] == '>') {
                t->self_closing = true;
                p++;
                break;
            }
            continue;
        }

        /* Attribute name. The first character may be '='. */
        tok_attr *a = tok_new_attr(t);
        h2m_buf_putc(&a->name, lower(s[p]));
        p++;
        while (p < n && !is_ws(s[p]) && s[p] != '/' && s[p] != '>' && s[p] != '=') {
            h2m_buf_putc(&a->name, lower(s[p]));
            p++;
        }
        while (p < n && is_ws(s[p]))
            p++;
        if (p < n && s[p] == '=') {
            p++;
            while (p < n && is_ws(s[p]))
                p++;
            if (p >= n) {
                t->pos = n;
                return false;
            }
            if (s[p] == '"' || s[p] == '\'') {
                char q = s[p++];
                const char *close = memchr(s + p, q, n - p);
                if (!close) {
                    t->pos = n;
                    return false;
                }
                size_t end = (size_t)(close - s);
                read_attr_value(t, &a->value, p, end);
                p = end + 1;
            } else if (s[p] != '>') {
                size_t start = p;
                while (p < n && !is_ws(s[p]) && s[p] != '>')
                    p++;
                read_attr_value(t, &a->value, start, p);
            }
        }
        /* Duplicate attributes are dropped; end tags carry none. Beyond
         * MAX_ATTRS the rest are parsed but ignored (DoS guard). */
        if (end_tag || t->nattrs > MAX_ATTRS || tok_has_attr(t, t->nattrs - 1, h2m_buf_cstr(&a->name)))
            t->nattrs--;
    }

    t->pos = p;
    t->type = end_tag ? TOK_END : TOK_START;
    if (!t->tag.data)
        h2m_buf_puts(&t->tag, "");
    return true;
}

/* Finds the appropriate end tag for RCDATA/RAWTEXT content. */
static size_t find_raw_end(const tokenizer *t)
{
    size_t p = t->pos;
    while (p < t->len) {
        const char *lt = memchr(t->src + p, '<', t->len - p);
        if (!lt)
            return t->len;
        p = (size_t)(lt - t->src);
        if (p + 2 + t->end_tag_len <= t->len && t->src[p + 1] == '/') {
            size_t i = 0;
            while (i < t->end_tag_len && lower(t->src[p + 2 + i]) == t->end_tag[i])
                i++;
            if (i == t->end_tag_len) {
                size_t after = p + 2 + i;
                if (after >= t->len || is_ws(t->src[after]) || t->src[after] == '/' ||
                    t->src[after] == '>')
                    return p;
            }
        }
        p++;
    }
    return t->len;
}

static void tok_next(tokenizer *t)
{
    h2m_buf_clear(&t->text);

    if (t->mode == MODE_PLAINTEXT) {
        if (t->pos < t->len) {
            h2m_buf_append(&t->text, t->src + t->pos, t->len - t->pos);
            t->pos = t->len;
            t->type = TOK_TEXT;
        } else {
            t->type = TOK_EOF;
        }
        return;
    }

    if (t->mode == MODE_RCDATA || t->mode == MODE_RAWTEXT) {
        size_t end = find_raw_end(t);
        bool rcdata = t->mode == MODE_RCDATA;
        t->mode = MODE_DATA;
        if (end > t->pos) {
            if (rcdata)
                h2m_decode_entities(t->src + t->pos, end - t->pos, false, &t->text);
            else
                h2m_buf_append(&t->text, t->src + t->pos, end - t->pos);
            t->pos = end;
            t->type = TOK_TEXT;
            return;
        }
    }

    const char *s = t->src;
    size_t n = t->len;
    while (t->pos < n) {
        size_t p = t->pos;
        char c = s[p];
        if (c == '&') {
            size_t used = h2m_decode_charref(s + p, n - p, false, &t->text);
            if (used == 0) {
                h2m_buf_putc(&t->text, '&');
                used = 1;
            }
            t->pos += used;
            continue;
        }
        if (c != '<') {
            size_t q = p;
            while (q < n && s[q] != '<' && s[q] != '&')
                q++;
            h2m_buf_append(&t->text, s + p, q - p);
            t->pos = q;
            continue;
        }

        /* c == '<' */
        char c1 = p + 1 < n ? s[p + 1] : '\0';
        char c2 = p + 2 < n ? s[p + 2] : '\0';
        bool markup = is_alpha(c1) || c1 == '!' || c1 == '?' || (c1 == '/' && (is_alpha(c2) || c2 == '>')) ||
                      (c1 == '/' && p + 2 < n);
        if (!markup) {
            h2m_buf_putc(&t->text, '<');
            t->pos++;
            continue;
        }
        if (t->text.len) {
            t->type = TOK_TEXT;
            return;
        }

        if (is_alpha(c1)) {
            t->pos = p + 1;
            if (parse_tag(t, false))
                return;
            continue;
        }
        if (c1 == '/') {
            if (is_alpha(c2)) {
                t->pos = p + 2;
                if (parse_tag(t, true))
                    return;
                continue;
            }
            if (c2 == '>') {
                t->pos = p + 3; /* "</>" is ignored */
                continue;
            }
            t->pos = p + 2; /* "</ ..." is a bogus comment */
            skip_to_gt(t);
            continue;
        }
        if (c1 == '!') {
            if (p + 3 < n && s[p + 2] == '-' && s[p + 3] == '-') {
                skip_comment(t);
                continue;
            }
            if (starts_with_ci(s + p + 2, n - p - 2, "[cdata[") && t->cdata_allowed) {
                size_t start = p + 9;
                size_t q = start;
                while (q + 2 < n && !(s[q] == ']' && s[q + 1] == ']' && s[q + 2] == '>'))
                    q++;
                if (q + 2 >= n) {
                    h2m_buf_append(&t->text, s + start, n - start);
                    t->pos = n;
                } else {
                    h2m_buf_append(&t->text, s + start, q - start);
                    t->pos = q + 3;
                }
                continue;
            }
            t->pos = p + 2;
            skip_to_gt(t);
            continue;
        }
        /* c1 == '?': processing instruction -> bogus comment */
        t->pos = p + 1;
        skip_to_gt(t);
    }

    t->type = t->text.len ? TOK_TEXT : TOK_EOF;
}

/* ------------------------------------------------------------------------ */
/* Tree builder                                                             */
/* ------------------------------------------------------------------------ */

typedef enum {
    IM_INITIAL,   /* before <html> / <head> */
    IM_IN_HEAD,
    IM_AFTER_HEAD,
    IM_IN_BODY
} insertion_mode;

typedef enum {
    SCOPE_DEFAULT,
    SCOPE_LIST_ITEM,
    SCOPE_BUTTON,
    SCOPE_TABLE,
    SCOPE_SELECT
} scope_kind;

typedef struct {
    h2m_node *node; /* NULL for a marker */
} afe_entry;

#define AFE_LIMIT 256 /* bound on active formatting elements (DoS guard) */

typedef struct {
    h2m_document *doc;
    h2m_arena *arena;
    tokenizer tok;
    insertion_mode mode;

    h2m_node **stack;
    size_t depth;
    size_t cap;

    afe_entry *afe;
    size_t nafe;
    size_t cap_afe;

    h2m_node *html;
    h2m_node *head;
    h2m_node *body;

    /* Pending text, coalesced into one text node when flushed. */
    h2m_buf pending;
    h2m_node *pending_parent;
    h2m_node *pending_before;

    bool skip_newline;
    bool foster; /* foster parenting enabled for the current insertion */
} builder;

static h2m_node *current(const builder *b)
{
    return b->depth ? b->stack[b->depth - 1] : b->doc->root;
}

static bool is_html(const h2m_node *n, h2m_tag tag)
{
    return n && n->type == H2M_NODE_ELEMENT && n->ns == H2M_NS_HTML && n->tag == tag;
}

static bool is_heading(const h2m_node *n)
{
    return n && n->type == H2M_NODE_ELEMENT && n->ns == H2M_NS_HTML && h2m_tag_is_heading(n->tag);
}

static bool name_is(const h2m_node *n, const char *name)
{
    return n && n->type == H2M_NODE_ELEMENT && strcmp(n->name, name) == 0;
}

/* Foreign elements that behave as "special" and as scope boundaries. */
static bool is_foreign_boundary(const h2m_node *n)
{
    if (n->ns == H2M_NS_MATHML)
        return name_is(n, "mi") || name_is(n, "mo") || name_is(n, "mn") || name_is(n, "ms") ||
               name_is(n, "mtext") || name_is(n, "annotation-xml");
    if (n->ns == H2M_NS_SVG)
        return name_is(n, "foreignobject") || name_is(n, "desc") || name_is(n, "title");
    return false;
}

static bool is_special(const h2m_node *n)
{
    if (n->ns != H2M_NS_HTML)
        return is_foreign_boundary(n);
    return h2m_tag_has(n->tag, TF_SPECIAL);
}

static bool is_scope_boundary(const h2m_node *n, scope_kind kind)
{
    if (n->ns != H2M_NS_HTML)
        return kind != SCOPE_TABLE && kind != SCOPE_SELECT && is_foreign_boundary(n);
    switch (kind) {
    case SCOPE_TABLE:
        return n->tag == TAG_HTML || n->tag == TAG_TABLE || n->tag == TAG_TEMPLATE;
    case SCOPE_SELECT:
        return n->tag != TAG_OPTGROUP && n->tag != TAG_OPTION;
    case SCOPE_LIST_ITEM:
        if (n->tag == TAG_OL || n->tag == TAG_UL)
            return true;
        break;
    case SCOPE_BUTTON:
        if (n->tag == TAG_BUTTON)
            return true;
        break;
    default:
        break;
    }
    return h2m_tag_has(n->tag, TF_SCOPE);
}

static bool has_in_scope(const builder *b, h2m_tag tag, scope_kind kind)
{
    for (size_t i = b->depth; i-- > 0;) {
        const h2m_node *n = b->stack[i];
        if (is_html(n, tag))
            return true;
        if (is_scope_boundary(n, kind))
            return false;
    }
    return false;
}

static bool has_heading_in_scope(const builder *b)
{
    for (size_t i = b->depth; i-- > 0;) {
        const h2m_node *n = b->stack[i];
        if (is_heading(n))
            return true;
        if (is_scope_boundary(n, SCOPE_DEFAULT))
            return false;
    }
    return false;
}

static bool has_cell_in_table_scope(const builder *b)
{
    return has_in_scope(b, TAG_TD, SCOPE_TABLE) || has_in_scope(b, TAG_TH, SCOPE_TABLE);
}

static bool has_node_in_scope(const builder *b, const h2m_node *target)
{
    for (size_t i = b->depth; i-- > 0;) {
        const h2m_node *n = b->stack[i];
        if (n == target)
            return true;
        if (is_scope_boundary(n, SCOPE_DEFAULT))
            return false;
    }
    return false;
}

static long stack_index(const builder *b, const h2m_node *n)
{
    for (size_t i = b->depth; i-- > 0;) {
        if (b->stack[i] == n)
            return (long)i;
    }
    return -1;
}

static void stack_insert(builder *b, size_t idx, h2m_node *n)
{
    if (b->depth == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->stack = h2m_xrealloc(b->stack, b->cap * sizeof *b->stack);
    }
    memmove(b->stack + idx + 1, b->stack + idx, (b->depth - idx) * sizeof *b->stack);
    b->stack[idx] = n;
    b->depth++;
}

static void stack_remove(builder *b, size_t idx)
{
    memmove(b->stack + idx, b->stack + idx + 1, (b->depth - idx - 1) * sizeof *b->stack);
    b->depth--;
}

static void afe_clear_to_marker(builder *b);

static void push(builder *b, h2m_node *n)
{
    stack_insert(b, b->depth, n);
}

static void pop(builder *b)
{
    if (b->depth == 0)
        return;
    h2m_node *n = b->stack[--b->depth];
    if (n->ns == H2M_NS_HTML &&
        (n->tag == TAG_TD || n->tag == TAG_TH || n->tag == TAG_CAPTION || n->tag == TAG_APPLET ||
         n->tag == TAG_OBJECT || n->tag == TAG_MARQUEE || n->tag == TAG_TEMPLATE))
        afe_clear_to_marker(b);
}

/* Pops until an HTML element with `tag` has been popped. */
static void pop_until(builder *b, h2m_tag tag)
{
    while (b->depth > 1) {
        bool match = is_html(current(b), tag);
        pop(b);
        if (match)
            return;
    }
}

static void pop_until_heading(builder *b)
{
    while (b->depth > 1) {
        bool match = is_heading(current(b));
        pop(b);
        if (match)
            return;
    }
}

static void generate_implied_end_tags(builder *b, h2m_tag except)
{
    for (;;) {
        h2m_node *n = current(b);
        if (b->depth <= 1 || n->ns != H2M_NS_HTML || n->tag == except || !h2m_tag_has(n->tag, TF_IMPLIED))
            return;
        pop(b);
    }
}

static void close_p_in_button_scope(builder *b)
{
    if (has_in_scope(b, TAG_P, SCOPE_BUTTON)) {
        generate_implied_end_tags(b, TAG_P);
        pop_until(b, TAG_P);
    }
}

static bool is_table_context(const h2m_node *n)
{
    return is_html(n, TAG_TABLE) || is_html(n, TAG_TBODY) || is_html(n, TAG_THEAD) ||
           is_html(n, TAG_TFOOT) || is_html(n, TAG_TR);
}

/* --- text coalescing ---------------------------------------------------- */

static void flush_text(builder *b)
{
    if (!b->pending.len || !b->pending_parent) {
        h2m_buf_clear(&b->pending);
        return;
    }
    h2m_node *t = h2m_node_new_text(b->arena, b->pending.data, b->pending.len);
    if (b->pending_before && b->pending_before->parent == b->pending_parent)
        h2m_node_insert_before(b->pending_parent, t, b->pending_before);
    else
        h2m_node_append(b->pending_parent, t);
    h2m_buf_clear(&b->pending);
    b->pending_parent = NULL;
    b->pending_before = NULL;
}

/* Computes the "appropriate place for inserting a node". */
static void insertion_place(builder *b, h2m_node *target, h2m_node **parent, h2m_node **before)
{
    *before = NULL;
    if (b->foster && is_table_context(target)) {
        h2m_node *table = NULL;
        long ti = -1;
        for (size_t i = b->depth; i-- > 0;) {
            if (is_html(b->stack[i], TAG_TABLE)) {
                table = b->stack[i];
                ti = (long)i;
                break;
            }
        }
        if (!table) {
            *parent = b->stack[0];
            return;
        }
        if (table->parent) {
            *parent = table->parent;
            *before = table;
            return;
        }
        *parent = ti > 0 ? b->stack[ti - 1] : b->stack[0];
        return;
    }
    *parent = target;
}

static void insert_node_at(builder *b, h2m_node *node, h2m_node *target)
{
    flush_text(b);
    h2m_node *parent, *before;
    insertion_place(b, target, &parent, &before);
    if (before)
        h2m_node_insert_before(parent, node, before);
    else
        h2m_node_append(parent, node);
}

static void insert_text(builder *b, const char *s, size_t len)
{
    if (len == 0)
        return;
    h2m_node *parent, *before;
    insertion_place(b, current(b), &parent, &before);
    if (b->pending.len && (b->pending_parent != parent || b->pending_before != before))
        flush_text(b);
    b->pending_parent = parent;
    b->pending_before = before;
    h2m_buf_append(&b->pending, s, len);
}

/* --- element creation --------------------------------------------------- */

static h2m_node *create_element(builder *b, h2m_namespace ns)
{
    tokenizer *t = &b->tok;
    h2m_tag tag = ns == H2M_NS_HTML ? h2m_tag_lookup(t->tag.data, t->tag.len) : TAG_UNKNOWN;
    if (tag == TAG_IMAGE)
        tag = TAG_IMG;
    h2m_node *n = h2m_node_new_element(b->arena, tag, t->tag.data, t->tag.len);
    n->ns = ns;
    if (t->nattrs) {
        n->attrs = h2m_arena_alloc(b->arena, t->nattrs * sizeof *n->attrs);
        n->nattrs = t->nattrs;
        for (size_t i = 0; i < t->nattrs; i++) {
            n->attrs[i].name = h2m_arena_strndup(b->arena, h2m_buf_cstr(&t->attrs[i].name), t->attrs[i].name.len);
            n->attrs[i].value = h2m_arena_strndup(b->arena, h2m_buf_cstr(&t->attrs[i].value), t->attrs[i].value.len);
        }
    }
    return n;
}

static h2m_node *clone_element(builder *b, const h2m_node *src)
{
    h2m_node *n = h2m_arena_alloc(b->arena, sizeof *n);
    n->type = H2M_NODE_ELEMENT;
    n->tag = src->tag;
    n->ns = src->ns;
    n->name = src->name;
    n->attrs = src->attrs; /* attributes are immutable and arena-owned */
    n->nattrs = src->nattrs;
    return n;
}

/* Inserts an element for the current token; pushes it unless `is_void`. */
static h2m_node *insert_element(builder *b, h2m_namespace ns, bool is_void)
{
    h2m_node *n = create_element(b, ns);
    insert_node_at(b, n, current(b));
    if (!is_void && b->depth < H2M_MAX_DEPTH)
        push(b, n);
    return n;
}

/* --- list of active formatting elements --------------------------------- */

static void afe_push_entry(builder *b, h2m_node *n)
{
    if (b->nafe == b->cap_afe) {
        b->cap_afe = b->cap_afe ? b->cap_afe * 2 : 16;
        b->afe = h2m_xrealloc(b->afe, b->cap_afe * sizeof *b->afe);
    }
    b->afe[b->nafe++].node = n;
}

static void afe_remove(builder *b, size_t idx)
{
    memmove(b->afe + idx, b->afe + idx + 1, (b->nafe - idx - 1) * sizeof *b->afe);
    b->nafe--;
}

static void afe_insert(builder *b, size_t idx, h2m_node *n)
{
    afe_push_entry(b, NULL);
    memmove(b->afe + idx + 1, b->afe + idx, (b->nafe - 1 - idx) * sizeof *b->afe);
    b->afe[idx].node = n;
}

static long afe_index(const builder *b, const h2m_node *n)
{
    for (size_t i = b->nafe; i-- > 0;) {
        if (b->afe[i].node == n)
            return (long)i;
    }
    return -1;
}

static void afe_push_marker(builder *b)
{
    afe_push_entry(b, NULL);
}

static void afe_clear_to_marker(builder *b)
{
    while (b->nafe) {
        h2m_node *n = b->afe[--b->nafe].node;
        if (!n)
            return;
    }
}

static bool same_attrs(const h2m_node *a, const h2m_node *c)
{
    if (a->nattrs != c->nattrs)
        return false;
    /* Fast path: same order (clones share their attribute array). */
    bool ordered = true;
    for (size_t i = 0; i < a->nattrs && ordered; i++) {
        ordered = strcmp(a->attrs[i].name, c->attrs[i].name) == 0 &&
                  strcmp(a->attrs[i].value, c->attrs[i].value) == 0;
    }
    if (ordered)
        return true;
    if (a->nattrs > 16)
        return false; /* treat as different: bounded cost, AFE stays bounded */
    for (size_t i = 0; i < a->nattrs; i++) {
        const char *v = h2m_node_attr(c, a->attrs[i].name);
        if (!v || strcmp(v, a->attrs[i].value) != 0)
            return false;
    }
    return true;
}

static void afe_push(builder *b, h2m_node *n)
{
    /* Only elements that made it onto the stack can be reconstructed. */
    if (stack_index(b, n) < 0)
        return;
    size_t count = 0, earliest = 0, since_marker = 0;
    for (size_t i = b->nafe; i-- > 0;) {
        h2m_node *e = b->afe[i].node;
        if (!e)
            break;
        since_marker++;
        if (e->tag == n->tag && e->ns == n->ns && strcmp(e->name, n->name) == 0 && same_attrs(e, n)) {
            count++;
            earliest = i;
        }
    }
    if (count >= 3) /* Noah's Ark clause */
        afe_remove(b, earliest);
    else if (since_marker >= AFE_LIMIT)
        return;
    afe_push_entry(b, n);
}

static void reconstruct_afe(builder *b)
{
    if (b->nafe == 0 || b->depth >= H2M_MAX_DEPTH)
        return;
    size_t i = b->nafe - 1;
    if (!b->afe[i].node || stack_index(b, b->afe[i].node) >= 0)
        return;
    while (i > 0) {
        i--;
        if (!b->afe[i].node || stack_index(b, b->afe[i].node) >= 0) {
            i++;
            break;
        }
    }
    for (; i < b->nafe; i++) {
        if (b->depth >= H2M_MAX_DEPTH)
            return;
        h2m_node *clone = clone_element(b, b->afe[i].node);
        insert_node_at(b, clone, current(b));
        push(b, clone);
        b->afe[i].node = clone;
    }
}

/* "Any other end tag" steps of the in-body insertion mode. */
static void any_other_end_tag(builder *b, h2m_tag tag, const char *name)
{
    for (size_t i = b->depth; i-- > 1;) {
        h2m_node *n = b->stack[i];
        bool match = n->ns == H2M_NS_HTML &&
                     (tag != TAG_UNKNOWN ? n->tag == tag : (n->tag == TAG_UNKNOWN && strcmp(n->name, name) == 0));
        if (match) {
            generate_implied_end_tags(b, tag != TAG_UNKNOWN ? tag : TAG_COUNT_);
            while (b->depth > i)
                pop(b);
            return;
        }
        if (is_special(n))
            return;
    }
}

static void adoption_agency(builder *b, h2m_tag tag, const char *name)
{
    flush_text(b);
    h2m_node *cur = current(b);
    if (is_html(cur, tag) && afe_index(b, cur) < 0) {
        pop(b);
        return;
    }

    for (int outer = 0; outer < 8; outer++) {
        long fi = -1;
        for (size_t i = b->nafe; i-- > 0;) {
            h2m_node *e = b->afe[i].node;
            if (!e)
                break;
            if (is_html(e, tag)) {
                fi = (long)i;
                break;
            }
        }
        if (fi < 0) {
            any_other_end_tag(b, tag, name);
            return;
        }
        h2m_node *fe = b->afe[fi].node;
        long si = stack_index(b, fe);
        if (si < 0) {
            afe_remove(b, (size_t)fi);
            return;
        }
        if (!has_node_in_scope(b, fe))
            return;

        long fbi = -1;
        for (size_t i = (size_t)si + 1; i < b->depth; i++) {
            if (is_special(b->stack[i])) {
                fbi = (long)i;
                break;
            }
        }
        if (fbi < 0) {
            while (b->depth > (size_t)si)
                pop(b);
            long ai = afe_index(b, fe);
            if (ai >= 0)
                afe_remove(b, (size_t)ai);
            return;
        }

        h2m_node *fb = b->stack[fbi];
        h2m_node *common = si > 0 ? b->stack[si - 1] : b->stack[0];
        long bookmark = fi;
        h2m_node *last = fb;
        long node_si = fbi;

        for (int inner = 1;; inner++) {
            node_si--;
            if (node_si < 0)
                break;
            h2m_node *node = b->stack[node_si];
            if (node == fe)
                break;
            long ai = afe_index(b, node);
            if (inner > 3 && ai >= 0) {
                afe_remove(b, (size_t)ai);
                if (ai < bookmark)
                    bookmark--;
                ai = -1;
            }
            if (ai < 0) {
                stack_remove(b, (size_t)node_si);
                continue;
            }
            h2m_node *clone = clone_element(b, node);
            b->afe[ai].node = clone;
            b->stack[node_si] = clone;
            if (last == fb)
                bookmark = ai + 1;
            h2m_node_append(clone, last);
            last = clone;
        }

        bool saved_foster = b->foster;
        b->foster = true;
        insert_node_at(b, last, common);
        b->foster = saved_foster;

        h2m_node *clone = clone_element(b, fe);
        while (fb->first_child)
            h2m_node_append(clone, fb->first_child);
        h2m_node_append(fb, clone);

        long fe_ai = afe_index(b, fe);
        if (fe_ai >= 0) {
            afe_remove(b, (size_t)fe_ai);
            if (fe_ai < bookmark)
                bookmark--;
        }
        if (bookmark < 0)
            bookmark = 0;
        if ((size_t)bookmark > b->nafe)
            bookmark = (long)b->nafe;
        afe_insert(b, (size_t)bookmark, clone);

        long fe_si = stack_index(b, fe);
        if (fe_si >= 0)
            stack_remove(b, (size_t)fe_si);
        long fb_si = stack_index(b, fb);
        stack_insert(b, (size_t)(fb_si + 1), clone);
    }
}

/* --- document structure ------------------------------------------------- */

static void ensure_html(builder *b)
{
    if (b->html)
        return;
    flush_text(b);
    b->html = h2m_node_new_element(b->arena, TAG_HTML, "html", 4);
    h2m_node_append(b->doc->root, b->html);
    push(b, b->html);
}

static void ensure_head(builder *b)
{
    ensure_html(b);
    if (b->head)
        return;
    flush_text(b);
    b->head = h2m_node_new_element(b->arena, TAG_HEAD, "head", 4);
    h2m_node_append(b->html, b->head);
    push(b, b->head);
    b->mode = IM_IN_HEAD;
}

static void ensure_body(builder *b)
{
    ensure_head(b);
    if (b->mode == IM_IN_HEAD) {
        /* Pop everything down to (and including) the head element. */
        while (b->depth > 1 && current(b) != b->head)
            pop(b);
        if (current(b) == b->head)
            pop(b);
        b->mode = IM_AFTER_HEAD;
    }
    if (!b->body) {
        flush_text(b);
        b->body = h2m_node_new_element(b->arena, TAG_BODY, "body", 4);
        h2m_node_append(b->html, b->body);
        push(b, b->body);
    }
    b->mode = IM_IN_BODY;
}

static bool is_head_content(h2m_tag tag)
{
    switch (tag) {
    case TAG_BASE: case TAG_BASEFONT: case TAG_BGSOUND: case TAG_LINK: case TAG_META:
    case TAG_TITLE: case TAG_STYLE: case TAG_SCRIPT: case TAG_NOSCRIPT: case TAG_TEMPLATE:
    case TAG_NOFRAMES:
        return true;
    default:
        return false;
    }
}

/* True while a <template> opened inside <head> is on the stack; its content
 * is parsed with the in-body rules but stays inside the template. */
static bool head_template_open(const builder *b)
{
    if (b->mode != IM_IN_HEAD)
        return false;
    for (size_t i = b->depth; i-- > 0;) {
        if (is_html(b->stack[i], TAG_TEMPLATE))
            return true;
        if (b->stack[i] == b->head)
            return false;
    }
    return false;
}

static void set_raw_mode(builder *b, tok_mode mode)
{
    tokenizer *t = &b->tok;
    size_t n = t->tag.len < sizeof t->end_tag ? t->tag.len : sizeof t->end_tag - 1;
    memcpy(t->end_tag, t->tag.data, n);
    t->end_tag_len = n;
    t->mode = mode;
}

/* Inserts a head-type element (at the current position). */
static void insert_head_element(builder *b, h2m_tag tag)
{
    bool is_void = h2m_tag_has(tag, TF_VOID) || b->tok.self_closing;
    if (tag == TAG_TITLE || tag == TAG_STYLE || tag == TAG_SCRIPT || tag == TAG_NOSCRIPT ||
        tag == TAG_NOFRAMES || tag == TAG_TEMPLATE)
        is_void = false;
    insert_element(b, H2M_NS_HTML, is_void);
    if (tag == TAG_TITLE)
        set_raw_mode(b, MODE_RCDATA);
    else if (tag == TAG_STYLE || tag == TAG_SCRIPT || tag == TAG_NOSCRIPT || tag == TAG_NOFRAMES)
        set_raw_mode(b, MODE_RAWTEXT);
    else if (tag == TAG_TEMPLATE)
        afe_push_marker(b);
}

/* --- in body ------------------------------------------------------------ */

static bool in_foreign_content(const builder *b)
{
    const h2m_node *n = current(b);
    return n->type == H2M_NODE_ELEMENT && n->ns != H2M_NS_HTML && !is_foreign_boundary(n);
}

static bool is_breakout_tag(const builder *b, h2m_tag tag)
{
    switch (tag) {
    case TAG_B: case TAG_BIG: case TAG_BLOCKQUOTE: case TAG_BODY: case TAG_BR: case TAG_CENTER:
    case TAG_CODE: case TAG_DD: case TAG_DIV: case TAG_DL: case TAG_DT: case TAG_EM: case TAG_EMBED:
    case TAG_H1: case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6: case TAG_HEAD:
    case TAG_HR: case TAG_I: case TAG_IMG: case TAG_LI: case TAG_LISTING: case TAG_MENU: case TAG_META:
    case TAG_NOBR: case TAG_OL: case TAG_P: case TAG_PRE: case TAG_RUBY: case TAG_S: case TAG_SMALL:
    case TAG_SPAN: case TAG_STRONG: case TAG_STRIKE: case TAG_SUB: case TAG_SUP: case TAG_TABLE:
    case TAG_TT: case TAG_U: case TAG_UL: case TAG_VAR:
        return true;
    case TAG_FONT:
        return tok_has_attr(&b->tok, b->tok.nattrs, "color") ||
               tok_has_attr(&b->tok, b->tok.nattrs, "face") ||
               tok_has_attr(&b->tok, b->tok.nattrs, "size");
    default:
        return false;
    }
}

static void close_cell(builder *b)
{
    generate_implied_end_tags(b, TAG_COUNT_);
    while (b->depth > 1) {
        h2m_node *n = current(b);
        bool cell = is_html(n, TAG_TD) || is_html(n, TAG_TH);
        pop(b);
        if (cell)
            return;
    }
}

static void clear_to_table_context(builder *b)
{
    while (b->depth > 1 && !is_html(current(b), TAG_TABLE) && !is_html(current(b), TAG_TEMPLATE))
        pop(b);
}

static void clear_to_body_context(builder *b)
{
    while (b->depth > 1) {
        h2m_node *n = current(b);
        if (is_html(n, TAG_TBODY) || is_html(n, TAG_THEAD) || is_html(n, TAG_TFOOT) ||
            is_html(n, TAG_TABLE) || is_html(n, TAG_TEMPLATE))
            return;
        pop(b);
    }
}

static void clear_to_row_context(builder *b)
{
    while (b->depth > 1) {
        h2m_node *n = current(b);
        if (is_html(n, TAG_TR) || is_html(n, TAG_TBODY) || is_html(n, TAG_THEAD) ||
            is_html(n, TAG_TFOOT) || is_html(n, TAG_TABLE) || is_html(n, TAG_TEMPLATE))
            return;
        pop(b);
    }
}

static void insert_implied(builder *b, h2m_tag tag)
{
    const char *name = h2m_tag_name(tag);
    h2m_node *n = h2m_node_new_element(b->arena, tag, name, strlen(name));
    insert_node_at(b, n, current(b));
    if (b->depth < H2M_MAX_DEPTH)
        push(b, n);
}

static void table_start_tag(builder *b, h2m_tag tag)
{
    if (!has_in_scope(b, TAG_TABLE, SCOPE_TABLE))
        return; /* table structure outside a table is ignored */

    switch (tag) {
    case TAG_TD:
    case TAG_TH:
        if (has_cell_in_table_scope(b))
            close_cell(b);
        clear_to_row_context(b);
        if (!is_html(current(b), TAG_TR))
            insert_implied(b, TAG_TR);
        insert_element(b, H2M_NS_HTML, false);
        afe_push_marker(b);
        return;
    case TAG_TR:
        if (has_cell_in_table_scope(b))
            close_cell(b);
        if (has_in_scope(b, TAG_TR, SCOPE_TABLE))
            pop_until(b, TAG_TR);
        clear_to_body_context(b);
        insert_element(b, H2M_NS_HTML, false);
        return;
    case TAG_TBODY:
    case TAG_THEAD:
    case TAG_TFOOT:
    case TAG_CAPTION:
    case TAG_COLGROUP:
        if (has_cell_in_table_scope(b))
            close_cell(b);
        clear_to_table_context(b);
        insert_element(b, H2M_NS_HTML, false);
        if (tag == TAG_CAPTION)
            afe_push_marker(b);
        return;
    case TAG_COL:
        if (has_cell_in_table_scope(b))
            close_cell(b);
        if (!is_html(current(b), TAG_COLGROUP))
            clear_to_table_context(b);
        insert_element(b, H2M_NS_HTML, true);
        return;
    default:
        return;
    }
}

static void process_start_tag(builder *b);

static void body_start_tag(builder *b)
{
    tokenizer *t = &b->tok;
    h2m_tag tag = h2m_tag_lookup(t->tag.data, t->tag.len);
    if (tag == TAG_IMAGE)
        tag = TAG_IMG;

    /* Foreign content (inside <svg> or <math>). */
    if (in_foreign_content(b)) {
        if (!is_breakout_tag(b, tag)) {
            h2m_namespace ns = current(b)->ns;
            if (strcmp(h2m_buf_cstr(&t->tag), "svg") == 0)
                ns = H2M_NS_SVG;
            else if (strcmp(h2m_buf_cstr(&t->tag), "math") == 0)
                ns = H2M_NS_MATHML;
            insert_element(b, ns, t->self_closing);
            return;
        }
        while (b->depth > 1 && in_foreign_content(b))
            pop(b);
    }

    /* Content that is not allowed directly inside table structure is
     * foster-parented before the table. */
    b->foster = false;
    if (is_table_context(current(b))) {
        switch (tag) {
        case TAG_CAPTION: case TAG_COLGROUP: case TAG_COL: case TAG_TBODY: case TAG_THEAD:
        case TAG_TFOOT: case TAG_TR: case TAG_TD: case TAG_TH: case TAG_SCRIPT: case TAG_STYLE:
        case TAG_TEMPLATE: case TAG_FORM:
            break;
        case TAG_TABLE:
            /* A table start tag directly in a table closes the open one. */
            pop_until(b, TAG_TABLE);
            process_start_tag(b);
            return;
        case TAG_INPUT: {
            bool hidden = false;
            for (size_t i = 0; i < t->nattrs; i++) {
                if (strcmp(h2m_buf_cstr(&t->attrs[i].name), "type") == 0 &&
                    h2m_buf_cstr(&t->attrs[i].value)[0] != '\0') {
                    const char *v = h2m_buf_cstr(&t->attrs[i].value);
                    hidden = (v[0] | 32) == 'h' && strlen(v) == 6 &&
                             ((v[1] | 32) == 'i') && ((v[2] | 32) == 'd') && ((v[3] | 32) == 'd') &&
                             ((v[4] | 32) == 'e') && ((v[5] | 32) == 'n');
                }
            }
            if (!hidden)
                b->foster = true;
            break;
        }
        default:
            b->foster = true;
            break;
        }
    }

    switch (tag) {
    case TAG_HTML:
    case TAG_BODY:
    case TAG_HEAD:
    case TAG_FRAMESET:
        break;

    case TAG_BASE: case TAG_BASEFONT: case TAG_BGSOUND: case TAG_LINK: case TAG_META:
    case TAG_NOFRAMES: case TAG_SCRIPT: case TAG_STYLE: case TAG_TEMPLATE: case TAG_TITLE:
        insert_head_element(b, tag);
        break;

    case TAG_ADDRESS: case TAG_ARTICLE: case TAG_ASIDE: case TAG_BLOCKQUOTE: case TAG_CENTER:
    case TAG_DETAILS: case TAG_DIALOG: case TAG_DIR: case TAG_DIV: case TAG_DL: case TAG_FIELDSET:
    case TAG_FIGCAPTION: case TAG_FIGURE: case TAG_FOOTER: case TAG_HEADER: case TAG_HGROUP:
    case TAG_MAIN: case TAG_MENU: case TAG_NAV: case TAG_OL: case TAG_P: case TAG_SEARCH:
    case TAG_SECTION: case TAG_SUMMARY: case TAG_UL:
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_H1: case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6:
        close_p_in_button_scope(b);
        if (is_heading(current(b)))
            pop(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_PRE:
    case TAG_LISTING:
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, false);
        b->skip_newline = true;
        break;

    case TAG_FORM:
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_LI:
    case TAG_DD:
    case TAG_DT:
        for (size_t i = b->depth; i-- > 1;) {
            h2m_node *n = b->stack[i];
            bool match = tag == TAG_LI ? is_html(n, TAG_LI) : (is_html(n, TAG_DD) || is_html(n, TAG_DT));
            if (match) {
                h2m_tag which = n->tag;
                generate_implied_end_tags(b, which);
                pop_until(b, which);
                break;
            }
            if (is_special(n) && !is_html(n, TAG_ADDRESS) && !is_html(n, TAG_DIV) && !is_html(n, TAG_P))
                break;
        }
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_PLAINTEXT:
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, false);
        t->mode = MODE_PLAINTEXT;
        break;

    case TAG_BUTTON:
        if (has_in_scope(b, TAG_BUTTON, SCOPE_DEFAULT)) {
            generate_implied_end_tags(b, TAG_COUNT_);
            pop_until(b, TAG_BUTTON);
        }
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_A: {
        for (size_t i = b->nafe; i-- > 0;) {
            h2m_node *e = b->afe[i].node;
            if (!e)
                break;
            if (is_html(e, TAG_A)) {
                adoption_agency(b, TAG_A, "a");
                long ai = afe_index(b, e);
                if (ai >= 0)
                    afe_remove(b, (size_t)ai);
                long si = stack_index(b, e);
                if (si >= 0)
                    stack_remove(b, (size_t)si);
                break;
            }
        }
        reconstruct_afe(b);
        h2m_node *n = insert_element(b, H2M_NS_HTML, false);
        afe_push(b, n);
        break;
    }

    case TAG_B: case TAG_BIG: case TAG_CODE: case TAG_EM: case TAG_FONT: case TAG_I: case TAG_S:
    case TAG_SMALL: case TAG_STRIKE: case TAG_STRONG: case TAG_TT: case TAG_U: {
        reconstruct_afe(b);
        h2m_node *n = insert_element(b, H2M_NS_HTML, false);
        afe_push(b, n);
        break;
    }

    case TAG_NOBR: {
        reconstruct_afe(b);
        if (has_in_scope(b, TAG_NOBR, SCOPE_DEFAULT)) {
            adoption_agency(b, TAG_NOBR, "nobr");
            reconstruct_afe(b);
        }
        h2m_node *n = insert_element(b, H2M_NS_HTML, false);
        afe_push(b, n);
        break;
    }

    case TAG_APPLET: case TAG_MARQUEE: case TAG_OBJECT:
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, false);
        afe_push_marker(b);
        break;

    case TAG_TABLE:
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_AREA: case TAG_BR: case TAG_EMBED: case TAG_IMG: case TAG_KEYGEN: case TAG_WBR:
    case TAG_INPUT:
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, true);
        break;

    case TAG_PARAM: case TAG_SOURCE: case TAG_TRACK:
        insert_element(b, H2M_NS_HTML, true);
        break;

    case TAG_HR:
        close_p_in_button_scope(b);
        insert_element(b, H2M_NS_HTML, true);
        break;

    case TAG_TEXTAREA:
        insert_element(b, H2M_NS_HTML, false);
        set_raw_mode(b, MODE_RCDATA);
        b->skip_newline = true;
        break;

    case TAG_XMP:
        close_p_in_button_scope(b);
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, false);
        set_raw_mode(b, MODE_RAWTEXT);
        break;

    case TAG_IFRAME: case TAG_NOEMBED: case TAG_NOSCRIPT:
        insert_element(b, H2M_NS_HTML, false);
        set_raw_mode(b, MODE_RAWTEXT);
        break;

    case TAG_SELECT:
        if (has_in_scope(b, TAG_SELECT, SCOPE_DEFAULT))
            pop_until(b, TAG_SELECT);
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_OPTGROUP:
    case TAG_OPTION:
        if (is_html(current(b), TAG_OPTION))
            pop(b);
        if (tag == TAG_OPTGROUP && is_html(current(b), TAG_OPTGROUP))
            pop(b);
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_RB:
    case TAG_RTC:
        if (has_in_scope(b, TAG_RUBY, SCOPE_DEFAULT))
            generate_implied_end_tags(b, TAG_COUNT_);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_RP:
    case TAG_RT:
        if (has_in_scope(b, TAG_RUBY, SCOPE_DEFAULT))
            generate_implied_end_tags(b, TAG_RTC);
        insert_element(b, H2M_NS_HTML, false);
        break;

    case TAG_MATH:
    case TAG_SVG:
        reconstruct_afe(b);
        insert_element(b, tag == TAG_MATH ? H2M_NS_MATHML : H2M_NS_SVG, t->self_closing);
        break;

    case TAG_CAPTION: case TAG_COL: case TAG_COLGROUP: case TAG_FRAME: case TAG_TBODY:
    case TAG_TD: case TAG_TFOOT: case TAG_TH: case TAG_THEAD: case TAG_TR:
        table_start_tag(b, tag);
        break;

    default:
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, false);
        break;
    }
    b->foster = false;
}

static void process_start_tag(builder *b)
{
    tokenizer *t = &b->tok;
    h2m_tag tag = h2m_tag_lookup(t->tag.data, t->tag.len);

    switch (b->mode) {
    case IM_INITIAL:
        ensure_html(b);
        if (tag == TAG_HTML) {
            /* Keep the attributes of the first <html> (lang is useful). */
            if (!b->html->nattrs && t->nattrs) {
                h2m_node *tmp = create_element(b, H2M_NS_HTML);
                b->html->attrs = tmp->attrs;
                b->html->nattrs = tmp->nattrs;
            }
            return;
        }
        if (tag == TAG_HEAD) {
            ensure_head(b);
            return;
        }
        if (is_head_content(tag)) {
            ensure_head(b);
            insert_head_element(b, tag);
            return;
        }
        ensure_body(b);
        body_start_tag(b);
        return;
    case IM_IN_HEAD:
        if (head_template_open(b)) {
            body_start_tag(b);
            return;
        }
        if (tag == TAG_HTML || tag == TAG_HEAD)
            return;
        if (is_head_content(tag)) {
            insert_head_element(b, tag);
            return;
        }
        ensure_body(b);
        body_start_tag(b);
        return;
    case IM_AFTER_HEAD:
    case IM_IN_BODY:
        if (tag == TAG_HTML && b->html && !b->html->nattrs && t->nattrs) {
            h2m_node *tmp = create_element(b, H2M_NS_HTML);
            b->html->attrs = tmp->attrs;
            b->html->nattrs = tmp->nattrs;
            return;
        }
        ensure_body(b);
        body_start_tag(b);
        return;
    }
}

static void process_end_tag(builder *b)
{
    tokenizer *t = &b->tok;
    h2m_tag tag = h2m_tag_lookup(t->tag.data, t->tag.len);
    const char *name = h2m_buf_cstr(&t->tag);

    bool in_template = head_template_open(b);
    if (!in_template && (b->mode == IM_INITIAL || b->mode == IM_IN_HEAD)) {
        if (tag == TAG_HEAD && b->mode == IM_IN_HEAD) {
            while (b->depth > 1 && current(b) != b->head)
                pop(b);
            if (current(b) == b->head)
                pop(b);
            b->mode = IM_AFTER_HEAD;
            return;
        }
        if (b->mode == IM_IN_HEAD && is_head_content(tag) && is_html(current(b), tag)) {
            pop(b);
            return;
        }
        if (tag != TAG_BR)
            return; /* stray end tag before the body: ignored */
    }
    if (tag == TAG_HTML || tag == TAG_BODY)
        return; /* content after </body> still belongs to the body */

    if (!in_template)
        ensure_body(b);

    /* End tags inside foreign content. */
    if (in_foreign_content(b) || (current(b)->ns != H2M_NS_HTML)) {
        for (size_t i = b->depth; i-- > 1;) {
            h2m_node *n = b->stack[i];
            if (n->ns == H2M_NS_HTML)
                break;
            if (strcmp(n->name, name) == 0) {
                while (b->depth > i)
                    pop(b);
                return;
            }
        }
    }

    switch (tag) {
    case TAG_ADDRESS: case TAG_ARTICLE: case TAG_ASIDE: case TAG_BLOCKQUOTE: case TAG_BUTTON:
    case TAG_CENTER: case TAG_DETAILS: case TAG_DIALOG: case TAG_DIR: case TAG_DIV: case TAG_DL:
    case TAG_FIELDSET: case TAG_FIGCAPTION: case TAG_FIGURE: case TAG_FOOTER: case TAG_HEADER:
    case TAG_HGROUP: case TAG_LISTING: case TAG_MAIN: case TAG_MENU: case TAG_NAV: case TAG_OL:
    case TAG_PRE: case TAG_SEARCH: case TAG_SECTION: case TAG_SUMMARY: case TAG_UL: case TAG_FORM:
    case TAG_APPLET: case TAG_MARQUEE: case TAG_OBJECT: case TAG_XMP: case TAG_SELECT:
        if (!has_in_scope(b, tag, SCOPE_DEFAULT))
            return;
        generate_implied_end_tags(b, TAG_COUNT_);
        pop_until(b, tag);
        return;

    case TAG_P:
        if (!has_in_scope(b, TAG_P, SCOPE_BUTTON)) {
            /* "</p>" without an open paragraph creates an empty one, which
             * still acts as a paragraph boundary. */
            h2m_node *p = h2m_node_new_element(b->arena, TAG_P, "p", 1);
            insert_node_at(b, p, current(b));
            return;
        }
        generate_implied_end_tags(b, TAG_P);
        pop_until(b, TAG_P);
        return;

    case TAG_LI:
        if (!has_in_scope(b, TAG_LI, SCOPE_LIST_ITEM))
            return;
        generate_implied_end_tags(b, TAG_LI);
        pop_until(b, TAG_LI);
        return;

    case TAG_DD:
    case TAG_DT:
        if (!has_in_scope(b, tag, SCOPE_DEFAULT))
            return;
        generate_implied_end_tags(b, tag);
        pop_until(b, tag);
        return;

    case TAG_H1: case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6:
        if (!has_heading_in_scope(b))
            return;
        generate_implied_end_tags(b, TAG_COUNT_);
        pop_until_heading(b);
        return;

    case TAG_A: case TAG_B: case TAG_BIG: case TAG_CODE: case TAG_EM: case TAG_FONT: case TAG_I:
    case TAG_NOBR: case TAG_S: case TAG_SMALL: case TAG_STRIKE: case TAG_STRONG: case TAG_TT:
    case TAG_U:
        adoption_agency(b, tag, name);
        return;

    case TAG_BR:
        /* "</br>" is treated as "<br>". */
        h2m_buf_clear(&t->tag);
        h2m_buf_puts(&t->tag, "br");
        t->nattrs = 0;
        t->self_closing = false;
        reconstruct_afe(b);
        insert_element(b, H2M_NS_HTML, true);
        return;

    case TAG_TABLE:
        if (has_in_scope(b, TAG_TABLE, SCOPE_TABLE))
            pop_until(b, TAG_TABLE);
        return;

    case TAG_TBODY: case TAG_THEAD: case TAG_TFOOT: case TAG_TR: case TAG_CAPTION:
        if (has_in_scope(b, tag, SCOPE_TABLE)) {
            generate_implied_end_tags(b, TAG_COUNT_);
            pop_until(b, tag);
        }
        return;

    case TAG_TD:
    case TAG_TH:
        if (has_in_scope(b, tag, SCOPE_TABLE)) {
            generate_implied_end_tags(b, TAG_COUNT_);
            pop_until(b, tag);
        }
        return;

    case TAG_COLGROUP:
        if (is_html(current(b), TAG_COLGROUP))
            pop(b);
        return;

    case TAG_COL:
    case TAG_HEAD:
    case TAG_FRAMESET:
        return;

    case TAG_TEMPLATE:
        if (has_in_scope(b, TAG_TEMPLATE, SCOPE_TABLE)) {
            generate_implied_end_tags(b, TAG_COUNT_);
            pop_until(b, TAG_TEMPLATE);
        }
        return;

    default:
        any_other_end_tag(b, tag, name);
        return;
    }
}

static void process_text(builder *b)
{
    tokenizer *t = &b->tok;
    const char *s = t->text.data;
    size_t len = t->text.len;

    if (b->skip_newline) {
        b->skip_newline = false;
        if (len && s[0] == '\n') {
            s++;
            len--;
        }
    }
    if (len == 0)
        return;

    if (b->mode == IM_IN_HEAD && current(b) != b->head) {
        /* Content of <title>, <style>, <script>, ... inside <head>. */
        insert_text(b, s, len);
        return;
    }
    if (b->mode != IM_IN_BODY) {
        size_t ws = 0;
        while (ws < len && is_ws(s[ws]))
            ws++;
        if (b->mode == IM_IN_HEAD && ws) {
            insert_text(b, s, ws); /* whitespace inside <head> is harmless */
        }
        s += ws;
        len -= ws;
        if (len == 0)
            return;
        ensure_body(b);
    }

    if (in_foreign_content(b)) {
        insert_text(b, s, len);
        return;
    }

    b->foster = false;
    if (is_table_context(current(b))) {
        bool all_ws = true;
        for (size_t i = 0; i < len; i++) {
            if (!is_ws(s[i])) {
                all_ws = false;
                break;
            }
        }
        if (all_ws) {
            insert_text(b, s, len);
            return;
        }
        b->foster = true;
    }
    reconstruct_afe(b);
    insert_text(b, s, len);
    b->foster = false;
}

void h2m_parse(h2m_document *doc, const char *html, size_t len)
{
    builder b;
    memset(&b, 0, sizeof b);
    b.doc = doc;
    b.arena = &doc->arena;
    b.mode = IM_INITIAL;
    tok_init(&b.tok, html, len);
    h2m_buf_init(&b.pending);

    for (;;) {
        tok_next(&b.tok);
        if (b.tok.type == TOK_EOF)
            break;
        if (b.tok.type == TOK_TEXT) {
            process_text(&b);
        } else {
            /* Pending text is flushed lazily, when the tree is mutated, so
             * text separated only by ignored tags forms one node. */
            b.skip_newline = false;
            if (b.tok.type == TOK_START)
                process_start_tag(&b);
            else
                process_end_tag(&b);
        }
        b.tok.cdata_allowed = in_foreign_content(&b);
    }
    flush_text(&b);
    ensure_body(&b);

    tok_free(&b.tok);
    h2m_buf_free(&b.pending);
    free(b.stack);
    free(b.afe);
}
