/*
 * Minimal DOM produced by the HTML parser. All memory is owned by the
 * document's arena.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_DOM_H
#define H2M_DOM_H

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "buf.h"
#include "tags.h"

typedef enum h2m_node_type {
    H2M_NODE_DOCUMENT,
    H2M_NODE_ELEMENT,
    H2M_NODE_TEXT
} h2m_node_type;

typedef enum h2m_namespace {
    H2M_NS_HTML = 0,
    H2M_NS_SVG,
    H2M_NS_MATHML
} h2m_namespace;

typedef struct h2m_attr {
    const char *name;  /* lowercase */
    const char *value; /* decoded, NUL-terminated */
} h2m_attr;

typedef struct h2m_node h2m_node;

struct h2m_node {
    h2m_node_type type;
    h2m_tag tag;          /* TAG_UNKNOWN for unlisted / foreign names */
    h2m_namespace ns;
    const char *name;     /* lowercase element name ("" for non-elements) */
    h2m_attr *attrs;
    size_t nattrs;
    char *text;           /* text node content */
    size_t text_len;
    h2m_node *parent;
    h2m_node *first_child;
    h2m_node *last_child;
    h2m_node *prev;
    h2m_node *next;
    unsigned flags;       /* H2M_NODE_F_* */
};

/* Node flags used by the converter's preprocessing pass. */
#define H2M_NODE_F_REMOVED 0x1u /* excluded from output */

typedef struct h2m_document {
    h2m_arena arena;
    h2m_node *root;       /* the Document node */
} h2m_document;

h2m_node *h2m_node_new_element(h2m_arena *a, h2m_tag tag, const char *name, size_t name_len);
h2m_node *h2m_node_new_text(h2m_arena *a, const char *text, size_t len);

void h2m_node_append(h2m_node *parent, h2m_node *child);
void h2m_node_insert_before(h2m_node *parent, h2m_node *child, h2m_node *ref);
void h2m_node_detach(h2m_node *node);

/* Returns the attribute value, or NULL when absent. */
const char *h2m_node_attr(const h2m_node *node, const char *name);

/* Whitespace-separated token match in the class attribute. */
bool h2m_node_has_class(const h2m_node *node, const char *cls);

/* True for an element of the given (HTML-namespace) tag. */
static inline bool h2m_node_is(const h2m_node *node, h2m_tag tag)
{
    return node && node->type == H2M_NODE_ELEMENT && node->tag == tag && node->ns == H2M_NS_HTML;
}

/* Next node in document order within `root` (pre-order traversal). */
h2m_node *h2m_node_next_in_tree(const h2m_node *node, const h2m_node *root, bool skip_children);

/* Appends the concatenated text of all descendant text nodes. */
void h2m_node_text_content(const h2m_node *node, h2m_buf *out);

/* Finds the first descendant element with the given tag (pre-order). */
h2m_node *h2m_node_find(const h2m_node *root, h2m_tag tag);

void h2m_document_init(h2m_document *doc);
void h2m_document_free(h2m_document *doc);

#endif /* H2M_DOM_H */
