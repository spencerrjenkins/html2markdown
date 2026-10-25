/*
 * CSS selector tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "encoding.h"
#include "parser.h"
#include "selector.h"
#include "testlib.h"

static const char *doc_html =
    "<div id=main class='content wide'>"
    "  <p class=intro lang=en-US>first</p>"
    "  <p data-x='hello world'>second</p>"
    "  <ul><li>a</li><li class=last>b</li></ul>"
    "  <span></span>"
    "</div>"
    "<aside><p>side</p></aside>";

/* Counts the elements of the parsed document matching `sel`. */
static int count_matches(const char *sel)
{
    char err[128];
    h2m_selector *s = h2m_selector_parse(sel, err, sizeof err);
    if (!s) {
        fprintf(stderr, "  selector '%s' failed to parse: %s\n", sel, err);
        return -1;
    }
    size_t len;
    char *decoded = h2m_decode_input(doc_html, strlen(doc_html), H2M_ENC_UTF8, &len, NULL, NULL, 0);
    h2m_document doc;
    h2m_document_init(&doc);
    h2m_parse(&doc, decoded, len);
    free(decoded);
    int count = 0;
    for (h2m_node *n = doc.root->first_child; n; n = h2m_node_next_in_tree(n, doc.root, false)) {
        if (h2m_selector_match(s, n))
            count++;
    }
    h2m_document_free(&doc);
    h2m_selector_free(s);
    return count;
}

static bool parses(const char *sel)
{
    char err[128];
    h2m_selector *s = h2m_selector_parse(sel, err, sizeof err);
    h2m_selector_free(s);
    return s != NULL;
}

void suite_selector(void)
{
    CHECK_EQ_INT(3, count_matches("p"));
    CHECK_EQ_INT(3, count_matches("P"));
    CHECK_EQ_INT(1, count_matches("#main"));
    CHECK_EQ_INT(1, count_matches(".content.wide"));
    CHECK_EQ_INT(1, count_matches("div.content"));
    CHECK_EQ_INT(0, count_matches("span.content"));
    CHECK_EQ_INT(2, count_matches("div p"));        /* descendant */
    CHECK_EQ_INT(2, count_matches("div > p"));      /* child */
    CHECK_EQ_INT(1, count_matches("p + p"));        /* next sibling */
    CHECK_EQ_INT(1, count_matches(".intro ~ ul"));  /* subsequent sibling */
    CHECK_EQ_INT(4, count_matches("div > p, li"));  /* list */
    CHECK_EQ_INT(1, count_matches("[data-x]"));
    CHECK_EQ_INT(1, count_matches("[data-x='hello world']"));
    CHECK_EQ_INT(1, count_matches("[data-x~=world]"));
    CHECK_EQ_INT(1, count_matches("[data-x^=hel]"));
    CHECK_EQ_INT(1, count_matches("[data-x$=\"rld\"]"));
    CHECK_EQ_INT(1, count_matches("[data-x*='o w']"));
    CHECK_EQ_INT(1, count_matches("[lang|=en]"));
    CHECK_EQ_INT(1, count_matches("[DATA-X='HELLO WORLD' i]"));
    CHECK_EQ_INT(0, count_matches("[data-x='HELLO WORLD']"));
    CHECK_EQ_INT(1, count_matches("li:first-child"));
    CHECK_EQ_INT(1, count_matches("li:last-child"));
    CHECK_EQ_INT(1, count_matches("li:not(.last)"));
    CHECK_EQ_INT(2, count_matches(":is(ul, aside)"));
    CHECK_EQ_INT(1, count_matches("span:empty"));
    CHECK_EQ_INT(1, count_matches("aside :only-child"));
    CHECK_EQ_INT(1, count_matches("body > aside > p"));

    CHECK(parses("  div  >  p  "));
    CHECK(parses("a[href^='http']:not([href*=example])"));
    CHECK(!parses(""));
    CHECK(!parses("div >"));
    CHECK(!parses("[unclosed"));
    CHECK(!parses("p::before"));
    CHECK(!parses(":hover"));
    CHECK(!parses("a,,b"));
    CHECK(!parses("div)"));
    CHECK(!parses(":not(div p)"));
    CHECK(!parses("[a='x]"));

    char err[128];
    CHECK(h2m_selector_parse("p::after", err, sizeof err) == NULL);
    CHECK(strstr(err, "pseudo-elements") != NULL);
}
