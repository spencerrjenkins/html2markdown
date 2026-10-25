/*
 * Minimal DOM.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dom.h"

#include <string.h>

h2m_node *h2m_node_new_element(h2m_arena *a, h2m_tag tag, const char *name, size_t name_len)
{
    h2m_node *n = h2m_arena_alloc(a, sizeof *n);
    n->type = H2M_NODE_ELEMENT;
    n->tag = tag;
    n->ns = H2M_NS_HTML;
    n->name = tag != TAG_UNKNOWN ? h2m_tag_name(tag) : h2m_arena_strndup(a, name, name_len);
    return n;
}

h2m_node *h2m_node_new_text(h2m_arena *a, const char *text, size_t len)
{
    h2m_node *n = h2m_arena_alloc(a, sizeof *n);
    n->type = H2M_NODE_TEXT;
    n->name = "";
    n->text = h2m_arena_strndup(a, text, len);
    n->text_len = len;
    return n;
}

void h2m_node_detach(h2m_node *node)
{
    h2m_node *p = node->parent;
    if (!p)
        return;
    if (node->prev)
        node->prev->next = node->next;
    else
        p->first_child = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        p->last_child = node->prev;
    node->parent = node->prev = node->next = NULL;
}

void h2m_node_append(h2m_node *parent, h2m_node *child)
{
    if (child->parent)
        h2m_node_detach(child);
    child->parent = parent;
    child->prev = parent->last_child;
    child->next = NULL;
    if (parent->last_child)
        parent->last_child->next = child;
    else
        parent->first_child = child;
    parent->last_child = child;
}

void h2m_node_insert_before(h2m_node *parent, h2m_node *child, h2m_node *ref)
{
    if (!ref) {
        h2m_node_append(parent, child);
        return;
    }
    if (child->parent)
        h2m_node_detach(child);
    child->parent = parent;
    child->next = ref;
    child->prev = ref->prev;
    if (ref->prev)
        ref->prev->next = child;
    else
        parent->first_child = child;
    ref->prev = child;
}

const char *h2m_node_attr(const h2m_node *node, const char *name)
{
    if (!node || node->type != H2M_NODE_ELEMENT)
        return NULL;
    for (size_t i = 0; i < node->nattrs; i++) {
        if (strcmp(node->attrs[i].name, name) == 0)
            return node->attrs[i].value;
    }
    return NULL;
}

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

bool h2m_node_has_class(const h2m_node *node, const char *cls)
{
    const char *v = h2m_node_attr(node, "class");
    if (!v)
        return false;
    size_t n = strlen(cls);
    const char *p = v;
    while (*p) {
        while (is_ws(*p))
            p++;
        const char *start = p;
        while (*p && !is_ws(*p))
            p++;
        if ((size_t)(p - start) == n && memcmp(start, cls, n) == 0)
            return true;
    }
    return false;
}

h2m_node *h2m_node_next_in_tree(const h2m_node *node, const h2m_node *root, bool skip_children)
{
    if (!skip_children && node->first_child)
        return node->first_child;
    while (node && node != root) {
        if (node->next)
            return node->next;
        node = node->parent;
    }
    return NULL;
}

void h2m_node_text_content(const h2m_node *node, h2m_buf *out)
{
    if (node->type == H2M_NODE_TEXT) {
        h2m_buf_append(out, node->text, node->text_len);
        return;
    }
    const h2m_node *n = node->first_child;
    while (n) {
        if (n->type == H2M_NODE_TEXT)
            h2m_buf_append(out, n->text, n->text_len);
        n = h2m_node_next_in_tree(n, node, false);
    }
}

h2m_node *h2m_node_find(const h2m_node *root, h2m_tag tag)
{
    h2m_node *n = root->first_child;
    while (n) {
        if (h2m_node_is(n, tag))
            return n;
        n = h2m_node_next_in_tree(n, root, false);
    }
    return NULL;
}

void h2m_document_init(h2m_document *doc)
{
    h2m_arena_init(&doc->arena);
    doc->root = h2m_arena_alloc(&doc->arena, sizeof *doc->root);
    doc->root->type = H2M_NODE_DOCUMENT;
    doc->root->name = "";
}

void h2m_document_free(h2m_document *doc)
{
    h2m_arena_free(&doc->arena);
    doc->root = NULL;
}
