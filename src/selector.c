/*
 * CSS selector parsing and matching.
 *
 * SPDX-License-Identifier: MIT
 */
#include "selector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"

typedef enum {
    S_TYPE,
    S_UNIVERSAL,
    S_ID,
    S_CLASS,
    S_ATTR,
    S_NOT,
    S_IS,
    S_FIRST_CHILD,
    S_LAST_CHILD,
    S_ONLY_CHILD,
    S_EMPTY
} simple_kind;

typedef enum {
    A_EXISTS,
    A_EQ,       /* =  */
    A_INCLUDES, /* ~= */
    A_DASH,     /* |= */
    A_PREFIX,   /* ^= */
    A_SUFFIX,   /* $= */
    A_SUBSTR    /* *= */
} attr_op;

typedef struct simple {
    simple_kind kind;
    attr_op op;
    bool icase;
    char *name;
    char *value;
    struct h2m_selector *sub; /* :not() / :is() argument */
} simple;

typedef struct {
    simple *items;
    size_t n;
} compound;

typedef struct {
    compound *parts;
    char *combs; /* combs[i] joins parts[i] and parts[i + 1] */
    size_t n;
} complex_sel;

struct h2m_selector {
    complex_sel *items;
    size_t n;
};

/* ------------------------------------------------------------------------ */
/* Parsing                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *s;
    size_t pos;
    char *err;
    size_t err_size;
    bool failed;
    int depth;
} parser;

static void fail(parser *p, const char *msg)
{
    if (p->failed)
        return;
    p->failed = true;
    if (p->err && p->err_size)
        snprintf(p->err, p->err_size, "%s at position %zu", msg, p->pos + 1);
}

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static void skip_ws(parser *p)
{
    while (is_ws(p->s[p->pos]))
        p->pos++;
}

static bool is_ident_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c >= 0x80;
}

/* Reads a CSS identifier (with backslash escapes). Returns NULL if absent. */
static char *parse_ident(parser *p, bool lowercase)
{
    h2m_buf b = H2M_BUF_INIT;
    for (;;) {
        unsigned char c = (unsigned char)p->s[p->pos];
        if (c == '\\' && p->s[p->pos + 1]) {
            h2m_buf_putc(&b, p->s[p->pos + 1]);
            p->pos += 2;
            continue;
        }
        if (!is_ident_char(c))
            break;
        if (lowercase && c >= 'A' && c <= 'Z')
            c = (unsigned char)(c + 32);
        h2m_buf_putc(&b, (char)c);
        p->pos++;
    }
    if (b.len == 0) {
        h2m_buf_free(&b);
        return NULL;
    }
    return h2m_buf_detach(&b, NULL);
}

static char *parse_string(parser *p)
{
    char q = p->s[p->pos++];
    h2m_buf b = H2M_BUF_INIT;
    while (p->s[p->pos] && p->s[p->pos] != q) {
        if (p->s[p->pos] == '\\' && p->s[p->pos + 1]) {
            h2m_buf_putc(&b, p->s[p->pos + 1]);
            p->pos += 2;
            continue;
        }
        h2m_buf_putc(&b, p->s[p->pos++]);
    }
    if (p->s[p->pos] != q) {
        h2m_buf_free(&b);
        fail(p, "unterminated string");
        return NULL;
    }
    p->pos++;
    return h2m_buf_detach(&b, NULL);
}

static void free_list(struct h2m_selector *sel);

static void free_compound(compound *c)
{
    for (size_t i = 0; i < c->n; i++) {
        free(c->items[i].name);
        free(c->items[i].value);
        if (c->items[i].sub)
            free_list(c->items[i].sub);
    }
    free(c->items);
}

static void free_complex(complex_sel *c)
{
    for (size_t i = 0; i < c->n; i++)
        free_compound(&c->parts[i]);
    free(c->parts);
    free(c->combs);
}

static void free_list(struct h2m_selector *sel)
{
    if (!sel)
        return;
    for (size_t i = 0; i < sel->n; i++)
        free_complex(&sel->items[i]);
    free(sel->items);
    free(sel);
}

static simple *add_simple(compound *c, simple_kind kind)
{
    c->items = h2m_xrealloc(c->items, (c->n + 1) * sizeof *c->items);
    simple *s = &c->items[c->n++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    return s;
}

static struct h2m_selector *parse_list(parser *p, bool compound_only, char terminator);

static bool parse_attr(parser *p, compound *c)
{
    p->pos++; /* '[' */
    skip_ws(p);
    char *name = parse_ident(p, true);
    if (!name) {
        fail(p, "expected attribute name");
        return false;
    }
    simple *s = add_simple(c, S_ATTR);
    s->name = name;
    s->op = A_EXISTS;
    skip_ws(p);
    char ch = p->s[p->pos];
    if (ch == ']') {
        p->pos++;
        return true;
    }
    if (ch == '=') {
        s->op = A_EQ;
        p->pos++;
    } else if (ch && p->s[p->pos + 1] == '=' && strchr("~|^$*", ch)) {
        s->op = ch == '~' ? A_INCLUDES : ch == '|' ? A_DASH : ch == '^' ? A_PREFIX : ch == '$' ? A_SUFFIX : A_SUBSTR;
        p->pos += 2;
    } else {
        fail(p, "expected ']' or an attribute operator");
        return false;
    }
    skip_ws(p);
    if (p->s[p->pos] == '"' || p->s[p->pos] == '\'') {
        s->value = parse_string(p);
        if (!s->value)
            return false;
    } else {
        s->value = parse_ident(p, false);
        if (!s->value) {
            fail(p, "expected attribute value");
            return false;
        }
    }
    skip_ws(p);
    if (p->s[p->pos] == 'i' || p->s[p->pos] == 'I') {
        s->icase = true;
        p->pos++;
        skip_ws(p);
    } else if (p->s[p->pos] == 's' || p->s[p->pos] == 'S') {
        p->pos++;
        skip_ws(p);
    }
    if (p->s[p->pos] != ']') {
        fail(p, "expected ']'");
        return false;
    }
    p->pos++;
    return true;
}

static bool parse_pseudo(parser *p, compound *c)
{
    p->pos++; /* ':' */
    if (p->s[p->pos] == ':') {
        fail(p, "pseudo-elements are not supported");
        return false;
    }
    char *name = parse_ident(p, true);
    if (!name) {
        fail(p, "expected pseudo-class name");
        return false;
    }
    bool ok = true;
    if (strcmp(name, "not") == 0 || strcmp(name, "is") == 0 || strcmp(name, "where") == 0) {
        if (p->s[p->pos] != '(') {
            fail(p, "expected '('");
            free(name);
            return false;
        }
        if (p->depth > 16) {
            fail(p, "selector nested too deeply");
            free(name);
            return false;
        }
        p->pos++;
        p->depth++;
        struct h2m_selector *sub = parse_list(p, true, ')');
        p->depth--;
        if (!sub) {
            free(name);
            return false;
        }
        if (p->s[p->pos] != ')') {
            free_list(sub);
            fail(p, "expected ')'");
            free(name);
            return false;
        }
        p->pos++;
        simple *s = add_simple(c, strcmp(name, "not") == 0 ? S_NOT : S_IS);
        s->sub = sub;
    } else if (strcmp(name, "first-child") == 0) {
        add_simple(c, S_FIRST_CHILD);
    } else if (strcmp(name, "last-child") == 0) {
        add_simple(c, S_LAST_CHILD);
    } else if (strcmp(name, "only-child") == 0) {
        add_simple(c, S_ONLY_CHILD);
    } else if (strcmp(name, "empty") == 0) {
        add_simple(c, S_EMPTY);
    } else {
        char msg[96];
        snprintf(msg, sizeof msg, "unsupported pseudo-class ':%.40s'", name);
        fail(p, msg);
        ok = false;
    }
    free(name);
    return ok;
}

static bool parse_compound(parser *p, compound *c)
{
    memset(c, 0, sizeof *c);
    unsigned char ch = (unsigned char)p->s[p->pos];
    if (ch == '*') {
        add_simple(c, S_UNIVERSAL);
        p->pos++;
    } else if (is_ident_char(ch) || ch == '\\') {
        char *name = parse_ident(p, true);
        simple *s = add_simple(c, S_TYPE);
        s->name = name;
    }
    for (;;) {
        ch = (unsigned char)p->s[p->pos];
        if (ch == '#' || ch == '.') {
            p->pos++;
            char *name = parse_ident(p, false);
            if (!name) {
                fail(p, ch == '#' ? "expected id after '#'" : "expected class name after '.'");
                return false;
            }
            simple *s = add_simple(c, ch == '#' ? S_ID : S_CLASS);
            s->name = name;
        } else if (ch == '[') {
            if (!parse_attr(p, c))
                return false;
        } else if (ch == ':') {
            if (!parse_pseudo(p, c))
                return false;
        } else {
            break;
        }
    }
    if (c->n == 0) {
        if (p->s[p->pos])
            fail(p, "unexpected character in selector");
        else
            fail(p, "expected a selector");
        return false;
    }
    return true;
}

static bool parse_complex(parser *p, complex_sel *out, bool compound_only)
{
    memset(out, 0, sizeof *out);
    for (;;) {
        compound c;
        if (!parse_compound(p, &c)) {
            free_compound(&c);
            return false;
        }
        out->parts = h2m_xrealloc(out->parts, (out->n + 1) * sizeof *out->parts);
        out->parts[out->n++] = c;

        size_t before = p->pos;
        skip_ws(p);
        char ch = p->s[p->pos];
        char comb = 0;
        if (ch == '>' || ch == '+' || ch == '~') {
            comb = ch;
            p->pos++;
            skip_ws(p);
        } else if (p->pos > before && ch && ch != ',' && ch != ')') {
            comb = ' ';
        }
        if (!comb)
            return true;
        if (compound_only) {
            fail(p, "combinators are not supported inside :not()/:is()");
            return false;
        }
        out->combs = h2m_xrealloc(out->combs, out->n * sizeof *out->combs);
        out->combs[out->n - 1] = comb;
    }
}

static struct h2m_selector *parse_list(parser *p, bool compound_only, char terminator)
{
    struct h2m_selector *sel = h2m_xcalloc(1, sizeof *sel);
    for (;;) {
        skip_ws(p);
        complex_sel c;
        if (!parse_complex(p, &c, compound_only)) {
            free_complex(&c);
            free_list(sel);
            return NULL;
        }
        sel->items = h2m_xrealloc(sel->items, (sel->n + 1) * sizeof *sel->items);
        sel->items[sel->n++] = c;
        skip_ws(p);
        if (p->s[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->s[p->pos] == terminator)
            return sel;
        fail(p, "unexpected character in selector");
        free_list(sel);
        return NULL;
    }
}

h2m_selector *h2m_selector_parse(const char *text, char *err, size_t err_size)
{
    if (err && err_size)
        err[0] = '\0';
    parser p = {text ? text : "", 0, err, err_size, false, 0};
    skip_ws(&p);
    if (!p.s[p.pos]) {
        fail(&p, "empty selector");
        return NULL;
    }
    return parse_list(&p, false, '\0');
}

void h2m_selector_free(h2m_selector *sel)
{
    free_list(sel);
}

/* ------------------------------------------------------------------------ */
/* Matching                                                                 */
/* ------------------------------------------------------------------------ */

static const h2m_node *parent_element(const h2m_node *n)
{
    const h2m_node *p = n->parent;
    return (p && p->type == H2M_NODE_ELEMENT) ? p : NULL;
}

static const h2m_node *prev_element(const h2m_node *n)
{
    for (const h2m_node *s = n->prev; s; s = s->prev) {
        if (s->type == H2M_NODE_ELEMENT)
            return s;
    }
    return NULL;
}

static const h2m_node *next_element(const h2m_node *n)
{
    for (const h2m_node *s = n->next; s; s = s->next) {
        if (s->type == H2M_NODE_ELEMENT)
            return s;
    }
    return NULL;
}

static char fold(char c, bool icase)
{
    return (icase && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool eq_n(const char *a, const char *b, size_t n, bool icase)
{
    for (size_t i = 0; i < n; i++) {
        if (fold(a[i], icase) != fold(b[i], icase))
            return false;
    }
    return true;
}

static bool match_attr(const simple *s, const h2m_node *n)
{
    const char *v = h2m_node_attr(n, s->name);
    if (!v)
        return false;
    if (s->op == A_EXISTS)
        return true;
    size_t vl = strlen(v), sl = strlen(s->value);
    switch (s->op) {
    case A_EQ:
        return vl == sl && eq_n(v, s->value, sl, s->icase);
    case A_PREFIX:
        return sl > 0 && vl >= sl && eq_n(v, s->value, sl, s->icase);
    case A_SUFFIX:
        return sl > 0 && vl >= sl && eq_n(v + vl - sl, s->value, sl, s->icase);
    case A_SUBSTR:
        if (sl == 0)
            return false;
        for (size_t i = 0; i + sl <= vl; i++) {
            if (eq_n(v + i, s->value, sl, s->icase))
                return true;
        }
        return false;
    case A_DASH:
        return vl >= sl && eq_n(v, s->value, sl, s->icase) && (vl == sl || v[sl] == '-');
    case A_INCLUDES: {
        if (sl == 0)
            return false;
        const char *p = v;
        while (*p) {
            while (is_ws(*p))
                p++;
            const char *start = p;
            while (*p && !is_ws(*p))
                p++;
            if ((size_t)(p - start) == sl && eq_n(start, s->value, sl, s->icase))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static bool match_simple(const simple *s, const h2m_node *n)
{
    switch (s->kind) {
    case S_UNIVERSAL:
        return true;
    case S_TYPE:
        return strcmp(n->name, s->name) == 0;
    case S_ID: {
        const char *id = h2m_node_attr(n, "id");
        return id && strcmp(id, s->name) == 0;
    }
    case S_CLASS:
        return h2m_node_has_class(n, s->name);
    case S_ATTR:
        return match_attr(s, n);
    case S_NOT:
        return !h2m_selector_match(s->sub, n);
    case S_IS:
        return h2m_selector_match(s->sub, n);
    case S_FIRST_CHILD:
        return prev_element(n) == NULL;
    case S_LAST_CHILD:
        return next_element(n) == NULL;
    case S_ONLY_CHILD:
        return prev_element(n) == NULL && next_element(n) == NULL;
    case S_EMPTY:
        for (const h2m_node *c = n->first_child; c; c = c->next) {
            if (c->type == H2M_NODE_ELEMENT || (c->type == H2M_NODE_TEXT && c->text_len))
                return false;
        }
        return true;
    }
    return false;
}

static bool match_compound(const compound *c, const h2m_node *n)
{
    for (size_t i = 0; i < c->n; i++) {
        if (!match_simple(&c->items[i], n))
            return false;
    }
    return true;
}

static bool match_complex(const complex_sel *c, size_t idx, const h2m_node *n)
{
    if (!match_compound(&c->parts[idx], n))
        return false;
    if (idx == 0)
        return true;
    switch (c->combs[idx - 1]) {
    case '>': {
        const h2m_node *p = parent_element(n);
        return p && match_complex(c, idx - 1, p);
    }
    case '+': {
        const h2m_node *p = prev_element(n);
        return p && match_complex(c, idx - 1, p);
    }
    case '~':
        for (const h2m_node *p = prev_element(n); p; p = prev_element(p)) {
            if (match_complex(c, idx - 1, p))
                return true;
        }
        return false;
    default:
        for (const h2m_node *p = parent_element(n); p; p = parent_element(p)) {
            if (match_complex(c, idx - 1, p))
                return true;
        }
        return false;
    }
}

bool h2m_selector_match(const h2m_selector *sel, const h2m_node *node)
{
    if (!sel || !node || node->type != H2M_NODE_ELEMENT)
        return false;
    for (size_t i = 0; i < sel->n; i++) {
        if (match_complex(&sel->items[i], sel->items[i].n - 1, node))
            return true;
    }
    return false;
}
