/*
 * Example: using libhtml2md from C.
 *
 * Build (after `make`):
 *     cc -Iinclude examples/library_usage.c build/libhtml2md.a -o example
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "html2md.h"

static void on_warning(void *user_data, const char *message)
{
    (void)user_data;
    fprintf(stderr, "warning: %s\n", message);
}

int main(void)
{
    static const char html[] =
        "<html><head><title>Example</title></head><body>"
        "<nav><a href=\"/\">Home</a></nav>"
        "<main><h1>Hello, <em>world</em></h1>"
        "<p>Visit <a href=\"docs/\">the docs</a> or read <code>README.md</code>.</p>"
        "<ul><li>Fast</li><li>Safe</li></ul></main>"
        "</body></html>";

    h2m_options opts;
    h2m_options_init(&opts);                    /* always start from the defaults */
    opts.main_content = true;                   /* drop the <nav> */
    opts.base_url = "https://example.com/";     /* absolute links */
    opts.link_style = H2M_LINKS_REFERENCE;      /* [text][1] */
    opts.warning = on_warning;

    char error[256] = "";
    opts.error_buffer = error;
    opts.error_buffer_size = sizeof error;

    char *markdown = NULL;
    size_t length = 0;
    h2m_status status = h2m_convert(html, sizeof html - 1, &opts, &markdown, &length);
    if (status != H2M_OK) {
        fprintf(stderr, "conversion failed: %s (%s)\n", h2m_status_string(status), error);
        return 1;
    }

    fwrite(markdown, 1, length, stdout);
    h2m_free(markdown);
    return 0;
}
