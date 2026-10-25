/*
 * HTML character reference decoding (named, decimal and hexadecimal) per the
 * WHATWG "character reference state", including legacy references that are
 * recognised without a trailing semicolon.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_ENTITIES_H
#define H2M_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>

#include "buf.h"

/*
 * Attempts to decode a character reference starting at s[0] == '&'.
 * On success, appends the UTF-8 expansion to `out` and returns the number of
 * input bytes consumed (> 0). Returns 0 when the text is not a character
 * reference, in which case the caller should emit '&' literally.
 *
 * `in_attribute` enables the attribute-value rule: a legacy reference without
 * ';' followed by '=' or an alphanumeric is left undecoded.
 */
size_t h2m_decode_charref(const char *s, size_t len, bool in_attribute, h2m_buf *out);

/* Decodes every character reference in s into out. */
void h2m_decode_entities(const char *s, size_t len, bool in_attribute, h2m_buf *out);

/* Exact lookup of an entity name (with its ';' if any). Test helper. */
bool h2m_entity_lookup(const char *name, size_t len, unsigned *cp1, unsigned *cp2);

#endif /* H2M_ENTITIES_H */
