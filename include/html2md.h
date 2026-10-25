/*
 * html2md - convert HTML to clean, readable Markdown.
 *
 * Public library interface. The library has no dependencies beyond the C
 * standard library, is reentrant (no global mutable state), and may be used
 * from multiple threads as long as each call uses its own buffers.
 *
 * Minimal example:
 *
 *     h2m_options opts;
 *     h2m_options_init(&opts);
 *     char *md = NULL;
 *     size_t md_len = 0;
 *     if (h2m_convert(html, html_len, &opts, &md, &md_len) == H2M_OK) {
 *         fwrite(md, 1, md_len, stdout);
 *         h2m_free(md);
 *     }
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HTML2MD_H
#define HTML2MD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H2M_VERSION_MAJOR 1
#define H2M_VERSION_MINOR 0
#define H2M_VERSION_PATCH 0
#define H2M_VERSION_STRING "1.0.0"

/* Result codes returned by the library. */
typedef enum h2m_status {
    H2M_OK = 0,
    H2M_ERR_INVALID_ARGUMENT, /* NULL pointer or out-of-range option */
    H2M_ERR_SELECTOR,         /* --select / --exclude selector failed to parse */
    H2M_ERR_ENCODING,         /* the requested input encoding is unsupported */
    H2M_ERR_NOMEM             /* memory allocation failed */
} h2m_status;

/* Markdown dialect to target. */
typedef enum h2m_flavor {
    H2M_FLAVOR_GFM = 0,     /* GitHub Flavored Markdown: tables, ~~strike~~, task lists */
    H2M_FLAVOR_COMMONMARK   /* strict CommonMark: extensions fall back to HTML or text */
} h2m_flavor;

typedef enum h2m_heading_style {
    H2M_HEADING_ATX = 0,    /* "# Title" */
    H2M_HEADING_SETEXT      /* "Title\n=====" for h1/h2, ATX for h3-h6 */
} h2m_heading_style;

typedef enum h2m_code_style {
    H2M_CODE_FENCED = 0,    /* ``` fenced blocks with language info string */
    H2M_CODE_INDENTED       /* four-space indented blocks */
} h2m_code_style;

typedef enum h2m_link_style {
    H2M_LINKS_INLINE = 0,   /* [text](url "title") */
    H2M_LINKS_REFERENCE,    /* [text][1] with definitions at the end */
    H2M_LINKS_TEXT          /* drop hyperlinks, keep their text */
} h2m_link_style;

typedef enum h2m_image_style {
    H2M_IMAGES_INLINE = 0,  /* ![alt](src "title") */
    H2M_IMAGES_ALT,         /* alt text only */
    H2M_IMAGES_NONE         /* drop images entirely */
} h2m_image_style;

typedef enum h2m_br_style {
    H2M_BR_BACKSLASH = 0,   /* "line\" + newline (visible, survives editors) */
    H2M_BR_SPACES           /* two trailing spaces + newline */
} h2m_br_style;

/* Receives non-fatal diagnostics (e.g. an unknown <meta charset>). */
typedef void (*h2m_warning_fn)(void *user_data, const char *message);

typedef struct h2m_options {
    h2m_flavor flavor;
    h2m_heading_style heading_style;
    h2m_code_style code_style;
    h2m_link_style link_style;
    h2m_image_style image_style;
    h2m_br_style br_style;

    char bullet;          /* '-', '*' or '+'                         (default '-') */
    char emphasis;        /* '*' or '_' for <em>                      (default '*') */
    char strong;          /* '*' or '_' for <strong> (doubled)        (default '*') */
    char fence;           /* '`' or '~' for fenced code blocks        (default '`') */

    int wrap;             /* wrap paragraphs at this column; 0 = never (default 0) */

    bool raw_html;        /* allow inline HTML where Markdown has no equivalent
                             (<sup>, <sub>, <kbd>, <br> in table cells, and
                             emphasis that cannot be expressed)  (default true) */
    bool main_content;    /* keep only main content, drop site chrome (default false) */
    bool keep_hidden;     /* keep hidden / aria-hidden / display:none (default false) */
    bool keep_data_uris;  /* keep images with data: URIs          (default false) */
    bool compact_tables;  /* do not pad table cells to align columns (default false) */
    bool preserve_nbsp;   /* keep U+00A0 instead of plain spaces  (default false) */
    bool front_matter;    /* prepend YAML front matter            (default false) */

    const char *base_url; /* resolve relative URLs against this (NULL = none;
                             a <base href> in the document is also honoured) */
    const char *select;   /* CSS selector list: convert only matches (NULL = all) */
    const char *exclude;  /* CSS selector list: remove matches (NULL = none) */
    const char *encoding; /* input encoding label (NULL = auto-detect) */

    h2m_warning_fn warning;   /* optional diagnostic callback */
    void *warning_data;       /* passed to warning() */

    /* Optional buffer that receives a human-readable message on failure. */
    char *error_buffer;
    size_t error_buffer_size;
} h2m_options;

/* Fill *opts with the documented defaults. Always call before customising. */
void h2m_options_init(h2m_options *opts);

/*
 * Convert `len` bytes of HTML to Markdown. On success *out receives a
 * NUL-terminated, heap-allocated string (free it with h2m_free) and *out_len
 * its length, excluding the terminator. On failure *out is set to NULL.
 */
h2m_status h2m_convert(const char *html, size_t len, const h2m_options *opts,
                       char **out, size_t *out_len);

/* Release memory returned by h2m_convert. Accepts NULL. */
void h2m_free(void *ptr);

/*
 * Check that `selector` is a supported CSS selector list. Returns true when
 * valid; otherwise writes a message to errbuf (if non-NULL) and returns false.
 */
bool h2m_selector_is_valid(const char *selector, char *errbuf, size_t errbuf_size);

/* Returns true when `label` names an input encoding the library can decode. */
bool h2m_encoding_is_supported(const char *label);

/* Human-readable description of a status code. */
const char *h2m_status_string(h2m_status status);

/* Library version, e.g. "1.0.0". */
const char *h2m_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HTML2MD_H */
