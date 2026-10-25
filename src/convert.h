/*
 * DOM to Markdown conversion.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_CONVERT_H
#define H2M_CONVERT_H

#include "html2md.h"
#include "buf.h"
#include "dom.h"
#include "selector.h"

typedef struct h2m_convert_input {
    const h2m_options *opts;
    const h2m_selector *select;  /* may be NULL */
    const h2m_selector *exclude; /* may be NULL */
} h2m_convert_input;

/* Converts the parsed document and appends Markdown to out. */
void h2m_convert_document(h2m_document *doc, const h2m_convert_input *in, h2m_buf *out);

#endif /* H2M_CONVERT_H */
