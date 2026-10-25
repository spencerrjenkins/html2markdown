/*
 * Public API: option handling and the decode -> parse -> convert pipeline.
 *
 * SPDX-License-Identifier: MIT
 */
#include "html2md.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "convert.h"
#include "dom.h"
#include "encoding.h"
#include "parser.h"
#include "selector.h"

void h2m_options_init(h2m_options *opts)
{
    if (!opts)
        return;
    memset(opts, 0, sizeof *opts);
    opts->flavor = H2M_FLAVOR_GFM;
    opts->heading_style = H2M_HEADING_ATX;
    opts->code_style = H2M_CODE_FENCED;
    opts->link_style = H2M_LINKS_INLINE;
    opts->image_style = H2M_IMAGES_INLINE;
    opts->br_style = H2M_BR_BACKSLASH;
    opts->bullet = '-';
    opts->emphasis = '*';
    opts->strong = '*';
    opts->fence = '`';
    opts->wrap = 0;
    opts->raw_html = true;
}

#if defined(__GNUC__) || defined(__clang__)
static void report(const h2m_options *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif

static void report(const h2m_options *o, const char *fmt, ...)
{
    if (!o->error_buffer || o->error_buffer_size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(o->error_buffer, o->error_buffer_size, fmt, ap);
    va_end(ap);
}

static bool valid_options(const h2m_options *o)
{
    if ((unsigned)o->flavor > H2M_FLAVOR_COMMONMARK) {
        report(o, "invalid flavor");
        return false;
    }
    if ((unsigned)o->heading_style > H2M_HEADING_SETEXT || (unsigned)o->code_style > H2M_CODE_INDENTED ||
        (unsigned)o->link_style > H2M_LINKS_TEXT || (unsigned)o->image_style > H2M_IMAGES_NONE ||
        (unsigned)o->br_style > H2M_BR_SPACES) {
        report(o, "invalid style option");
        return false;
    }
    if (o->bullet != '-' && o->bullet != '*' && o->bullet != '+') {
        report(o, "bullet must be '-', '*' or '+'");
        return false;
    }
    if ((o->emphasis != '*' && o->emphasis != '_') || (o->strong != '*' && o->strong != '_')) {
        report(o, "emphasis and strong delimiters must be '*' or '_'");
        return false;
    }
    if (o->fence != '`' && o->fence != '~') {
        report(o, "code fence must be '`' or '~'");
        return false;
    }
    if (o->wrap < 0) {
        report(o, "wrap width must not be negative");
        return false;
    }
    return true;
}

h2m_status h2m_convert(const char *html, size_t len, const h2m_options *opts, char **out, size_t *out_len)
{
    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0;

    h2m_options defaults;
    if (!opts) {
        h2m_options_init(&defaults);
        opts = &defaults;
    }
    if (opts->error_buffer && opts->error_buffer_size)
        opts->error_buffer[0] = '\0';
    if (!out || (!html && len)) {
        report(opts, "invalid argument");
        return H2M_ERR_INVALID_ARGUMENT;
    }
    if (!valid_options(opts))
        return H2M_ERR_INVALID_ARGUMENT;

    h2m_encoding forced = H2M_ENC_UNKNOWN;
    if (opts->encoding && *opts->encoding) {
        forced = h2m_encoding_lookup(opts->encoding, strlen(opts->encoding));
        if (forced == H2M_ENC_UNKNOWN) {
            report(opts, "unsupported encoding '%.64s'", opts->encoding);
            return H2M_ERR_ENCODING;
        }
    }

    char err[256];
    h2m_selector *select = NULL, *exclude = NULL;
    if (opts->select) {
        select = h2m_selector_parse(opts->select, err, sizeof err);
        if (!select) {
            report(opts, "invalid selector '%.100s': %s", opts->select, err);
            return H2M_ERR_SELECTOR;
        }
    }
    if (opts->exclude) {
        exclude = h2m_selector_parse(opts->exclude, err, sizeof err);
        if (!exclude) {
            h2m_selector_free(select);
            report(opts, "invalid selector '%.100s': %s", opts->exclude, err);
            return H2M_ERR_SELECTOR;
        }
    }

    size_t decoded_len = 0;
    char warn[160];
    char *decoded = h2m_decode_input(html ? html : "", len, forced, &decoded_len, NULL, warn, sizeof warn);
    if (!decoded) {
        h2m_selector_free(select);
        h2m_selector_free(exclude);
        report(opts, "out of memory");
        return H2M_ERR_NOMEM;
    }
    if (warn[0] && opts->warning)
        opts->warning(opts->warning_data, warn);

    h2m_document doc;
    h2m_document_init(&doc);
    h2m_parse(&doc, decoded, decoded_len);
    free(decoded);

    h2m_buf md = H2M_BUF_INIT;
    h2m_convert_input in = {opts, select, exclude};
    h2m_convert_document(&doc, &in, &md);

    h2m_document_free(&doc);
    h2m_selector_free(select);
    h2m_selector_free(exclude);

    *out = h2m_buf_detach(&md, out_len);
    return H2M_OK;
}

void h2m_free(void *ptr)
{
    free(ptr);
}

bool h2m_selector_is_valid(const char *selector, char *errbuf, size_t errbuf_size)
{
    h2m_selector *sel = h2m_selector_parse(selector, errbuf, errbuf_size);
    if (!sel)
        return false;
    h2m_selector_free(sel);
    return true;
}

bool h2m_encoding_is_supported(const char *label)
{
    return label && h2m_encoding_lookup(label, strlen(label)) != H2M_ENC_UNKNOWN;
}

const char *h2m_status_string(h2m_status status)
{
    switch (status) {
    case H2M_OK: return "success";
    case H2M_ERR_INVALID_ARGUMENT: return "invalid argument";
    case H2M_ERR_SELECTOR: return "invalid CSS selector";
    case H2M_ERR_ENCODING: return "unsupported encoding";
    case H2M_ERR_NOMEM: return "out of memory";
    default: return "unknown error";
    }
}

const char *h2m_version(void)
{
    return H2M_VERSION_STRING;
}
