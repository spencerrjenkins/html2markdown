/*
 * HTML parser: a tokenizer and tree builder modelled on the WHATWG HTML
 * parsing algorithm. It never fails: any byte sequence yields a DOM, with
 * the same error recovery browsers apply to malformed markup (implied end
 * tags, misnested formatting via the adoption agency algorithm, foster
 * parenting of stray table content, and so on).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_PARSER_H
#define H2M_PARSER_H

#include <stddef.h>

#include "dom.h"

/* Maximum element nesting depth. Deeper elements become siblings of the
 * element at the limit (as in Chromium), bounding recursion elsewhere. */
#define H2M_MAX_DEPTH 512

/*
 * Parses `len` bytes of preprocessed UTF-8 (see h2m_decode_input) into doc,
 * which must have been initialised with h2m_document_init.
 */
void h2m_parse(h2m_document *doc, const char *html, size_t len);

#endif /* H2M_PARSER_H */
