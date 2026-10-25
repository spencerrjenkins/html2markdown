/*
 * A small CSS selector engine for --select / --exclude.
 *
 * Supported syntax:
 *   type selectors (div), universal (*), #id, .class,
 *   attribute selectors [a] [a=v] [a~=v] [a|=v] [a^=v] [a$=v] [a*=v] (+ " i" flag),
 *   pseudo-classes :not(), :is(), :where() (compound selector lists),
 *   :first-child, :last-child, :only-child, :empty,
 *   combinators: descendant (whitespace), child (>), next sibling (+),
 *   subsequent sibling (~), and selector lists separated by commas.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_SELECTOR_H
#define H2M_SELECTOR_H

#include <stdbool.h>
#include <stddef.h>

#include "dom.h"

typedef struct h2m_selector h2m_selector;

/* Parses a selector list. On error returns NULL and writes a message. */
h2m_selector *h2m_selector_parse(const char *text, char *err, size_t err_size);

/* True when the element matches any selector in the list. */
bool h2m_selector_match(const h2m_selector *sel, const h2m_node *node);

void h2m_selector_free(h2m_selector *sel);

#endif /* H2M_SELECTOR_H */
