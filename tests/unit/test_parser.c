/*
 * Tree-construction tests. Expected trees follow the html5lib test format
 * (and therefore browser behaviour) for the constructs they cover.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "buf.h"
#include "dom.h"
#include "encoding.h"
#include "parser.h"
#include "testlib.h"

static void dump(const h2m_node *n, int depth, h2m_buf *out)
{
    for (const h2m_node *c = n->first_child; c; c = c->next) {
        h2m_buf_puts(out, "|");
        for (int i = 0; i < depth; i++)
            h2m_buf_puts(out, "  ");
        h2m_buf_putc(out, ' ');
        if (c->type == H2M_NODE_TEXT) {
            h2m_buf_putc(out, '"');
            h2m_buf_append(out, c->text, c->text_len);
            h2m_buf_puts(out, "\"\n");
            continue;
        }
        h2m_buf_putc(out, '<');
        if (c->ns == H2M_NS_SVG)
            h2m_buf_puts(out, "svg ");
        else if (c->ns == H2M_NS_MATHML)
            h2m_buf_puts(out, "math ");
        h2m_buf_puts(out, c->name);
        h2m_buf_puts(out, ">\n");
        for (size_t i = 0; i < c->nattrs; i++) {
            h2m_buf_puts(out, "|");
            for (int k = 0; k < depth + 1; k++)
                h2m_buf_puts(out, "  ");
            h2m_buf_printf(out, " %s=\"%s\"\n", c->attrs[i].name, c->attrs[i].value);
        }
        dump(c, depth + 1, out);
    }
}

static char *parse_dump(const char *html)
{
    size_t len;
    char *decoded = h2m_decode_input(html, strlen(html), H2M_ENC_UTF8, &len, NULL, NULL, 0);
    h2m_document doc;
    h2m_document_init(&doc);
    h2m_parse(&doc, decoded, len);
    free(decoded);
    h2m_buf out = H2M_BUF_INIT;
    dump(doc.root, 0, &out);
    h2m_document_free(&doc);
    return h2m_buf_detach(&out, NULL);
}

#define CHECK_TREE(html, expected)          \
    do {                                    \
        char *t_ = parse_dump(html);        \
        CHECK_EQ_STR(expected, t_);         \
        free(t_);                           \
    } while (0)

static void test_basic(void)
{
    CHECK_TREE("", "| <html>\n|   <head>\n|   <body>\n");
    CHECK_TREE("Hello",
               "| <html>\n|   <head>\n|   <body>\n|     \"Hello\"\n");
    CHECK_TREE("<!DOCTYPE html><title>T</title><p>x",
               "| <html>\n|   <head>\n|     <title>\n|       \"T\"\n|   <body>\n|     <p>\n|       \"x\"\n");
    CHECK_TREE("<p>a<!-- c -->b<?pi?>c</p>",
               "| <html>\n|   <head>\n|   <body>\n|     <p>\n|       \"abc\"\n");
    CHECK_TREE("<div id=a class='b c' data-x=1 ID=dup>t</div>",
               "| <html>\n|   <head>\n|   <body>\n|     <div>\n|       id=\"a\"\n|       class=\"b c\"\n"
               "|       data-x=\"1\"\n|       \"t\"\n");
    CHECK_TREE("<a href=\"?a=1&amp;b=2&copy=3\">x</a>",
               "| <html>\n|   <head>\n|   <body>\n|     <a>\n|       href=\"?a=1&b=2&copy=3\"\n|       \"x\"\n");
}

static void test_implied_end_tags(void)
{
    CHECK_TREE("<p>one<p>two",
               "| <html>\n|   <head>\n|   <body>\n|     <p>\n|       \"one\"\n|     <p>\n|       \"two\"\n");
    CHECK_TREE("<ul><li>a<li>b</ul>",
               "| <html>\n|   <head>\n|   <body>\n|     <ul>\n|       <li>\n|         \"a\"\n"
               "|       <li>\n|         \"b\"\n");
    CHECK_TREE("<dl><dt>t<dd>d<dt>u</dl>",
               "| <html>\n|   <head>\n|   <body>\n|     <dl>\n|       <dt>\n|         \"t\"\n"
               "|       <dd>\n|         \"d\"\n|       <dt>\n|         \"u\"\n");
    CHECK_TREE("<p>para<div>block</div>",
               "| <html>\n|   <head>\n|   <body>\n|     <p>\n|       \"para\"\n|     <div>\n|       \"block\"\n");
    CHECK_TREE("<h1>a<h2>b</h1>c",
               "| <html>\n|   <head>\n|   <body>\n|     <h1>\n|       \"a\"\n|     <h2>\n|       \"b\"\n|     \"c\"\n");
    /* A stray </p> creates an empty paragraph. */
    CHECK_TREE("a</p>b",
               "| <html>\n|   <head>\n|   <body>\n|     \"a\"\n|     <p>\n|     \"b\"\n");
    /* </br> is treated as <br>. */
    CHECK_TREE("a</br>b",
               "| <html>\n|   <head>\n|   <body>\n|     \"a\"\n|     <br>\n|     \"b\"\n");
    /* Nested list inside li keeps the outer li open. */
    CHECK_TREE("<ul><li>a<ul><li>b</ul><li>c</ul>",
               "| <html>\n|   <head>\n|   <body>\n|     <ul>\n|       <li>\n|         \"a\"\n"
               "|         <ul>\n|           <li>\n|             \"b\"\n|       <li>\n|         \"c\"\n");
}

static void test_adoption_agency(void)
{
    /* html5lib tests1.dat */
    CHECK_TREE("<b>1<p>2</b>3</p>",
               "| <html>\n|   <head>\n|   <body>\n|     <b>\n|       \"1\"\n|     <p>\n|       <b>\n"
               "|         \"2\"\n|       \"3\"\n");
    CHECK_TREE("<a><p>X<a>Y</a>Z</p></a>",
               "| <html>\n|   <head>\n|   <body>\n|     <a>\n|     <p>\n|       <a>\n|         \"X\"\n"
               "|       <a>\n|         \"Y\"\n|       \"Z\"\n");
    CHECK_TREE("<b><i>x</b>y</i>",
               "| <html>\n|   <head>\n|   <body>\n|     <b>\n|       <i>\n|         \"x\"\n|     <i>\n"
               "|       \"y\"\n");
    /* Reconstruction of active formatting elements across paragraphs. */
    CHECK_TREE("<p><b><i>a</p><p>b",
               "| <html>\n|   <head>\n|   <body>\n|     <p>\n|       <b>\n|         <i>\n|           \"a\"\n"
               "|     <p>\n|       <b>\n|         <i>\n|           \"b\"\n");
    /* Nested <a> closes the outer one. */
    CHECK_TREE("<a href=1>a<a href=2>b</a>",
               "| <html>\n|   <head>\n|   <body>\n|     <a>\n|       href=\"1\"\n|       \"a\"\n|     <a>\n"
               "|       href=\"2\"\n|       \"b\"\n");
    /* Unmatched end tags are ignored. */
    CHECK_TREE("<div>a</span>b</div>",
               "| <html>\n|   <head>\n|   <body>\n|     <div>\n|       \"ab\"\n");
}

static void test_tables(void)
{
    /* Foster parenting of text and elements misplaced in a table. */
    CHECK_TREE("<table>X<tr><td>1</td></tr></table>",
               "| <html>\n|   <head>\n|   <body>\n|     \"X\"\n|     <table>\n|       <tr>\n|         <td>\n"
               "|           \"1\"\n");
    CHECK_TREE("<table><div>d</div><tr><td>c</table>",
               "| <html>\n|   <head>\n|   <body>\n|     <div>\n|       \"d\"\n|     <table>\n|       <tr>\n"
               "|         <td>\n|           \"c\"\n");
    /* Implied rows and cells. */
    CHECK_TREE("<table><td>a<td>b<tr><th>c</table>",
               "| <html>\n|   <head>\n|   <body>\n|     <table>\n|       <tr>\n|         <td>\n"
               "|           \"a\"\n|         <td>\n|           \"b\"\n|       <tr>\n|         <th>\n"
               "|           \"c\"\n");
    /* Cells outside tables are ignored. */
    CHECK_TREE("<td>x</td>",
               "| <html>\n|   <head>\n|   <body>\n|     \"x\"\n");
    /* Table closes an open paragraph. */
    CHECK_TREE("<p>a<table><tr><td>b</table>",
               "| <html>\n|   <head>\n|   <body>\n|     <p>\n|       \"a\"\n|     <table>\n|       <tr>\n"
               "|         <td>\n|           \"b\"\n");
}

static void test_raw_text(void)
{
    CHECK_TREE("<script>if (a < b && c) { x = '</div>'; }</script>",
               "| <html>\n|   <head>\n|     <script>\n|       \"if (a < b && c) { x = '</div>'; }\"\n|   <body>\n");
    CHECK_TREE("<textarea>\n&lt;b&gt;</textarea>",
               "| <html>\n|   <head>\n|   <body>\n|     <textarea>\n|       \"<b>\"\n");
    CHECK_TREE("<pre>\n\nx</pre>",
               "| <html>\n|   <head>\n|   <body>\n|     <pre>\n|       \"\nx\"\n");
    CHECK_TREE("<style>p{}</STYLE >after",
               "| <html>\n|   <head>\n|     <style>\n|       \"p{}\"\n|   <body>\n|     \"after\"\n");
    CHECK_TREE("<plaintext><b>x</b>",
               "| <html>\n|   <head>\n|   <body>\n|     <plaintext>\n|       \"<b>x</b>\"\n");
    CHECK_TREE("<title>a<b>c</title>",
               "| <html>\n|   <head>\n|     <title>\n|       \"a<b>c\"\n|   <body>\n");
}

static void test_foreign_and_misc(void)
{
    CHECK_TREE("<svg><circle/><p>x",
               "| <html>\n|   <head>\n|   <body>\n|     <svg svg>\n|       <svg circle>\n|     <p>\n"
               "|       \"x\"\n");
    CHECK_TREE("<math><mi>x</mi></math>",
               "| <html>\n|   <head>\n|   <body>\n|     <math math>\n|       <math mi>\n|         \"x\"\n");
    CHECK_TREE("<svg><![CDATA[a<b]]></svg>",
               "| <html>\n|   <head>\n|   <body>\n|     <svg svg>\n|       \"a<b\"\n");
    CHECK_TREE("a < b <3 </ c",
               "| <html>\n|   <head>\n|   <body>\n|     \"a < b <3 \"\n");
    CHECK_TREE("<p>x<div",
               "| <html>\n|   <head>\n|   <body>\n|     <p>\n|       \"x\"\n");
    CHECK_TREE("<img src=a><br/><input type=hidden>",
               "| <html>\n|   <head>\n|   <body>\n|     <img>\n|       src=\"a\"\n|     <br>\n"
               "|     <input>\n|       type=\"hidden\"\n");
    CHECK_TREE("<body>a</body></html>b",
               "| <html>\n|   <head>\n|   <body>\n|     \"ab\"\n");
}

static void test_depth_limit(void)
{
    /* Deep nesting is capped rather than overflowing anything. */
    h2m_buf html = H2M_BUF_INIT;
    for (int i = 0; i < 100000; i++)
        h2m_buf_puts(&html, "<div>");
    h2m_buf_puts(&html, "deep");
    size_t len;
    char *decoded = h2m_decode_input(html.data, html.len, H2M_ENC_UTF8, &len, NULL, NULL, 0);
    h2m_document doc;
    h2m_document_init(&doc);
    h2m_parse(&doc, decoded, len);
    int depth = 0;
    const h2m_node *n = doc.root;
    while (n->first_child) {
        n = n->last_child;
        depth++;
    }
    CHECK(depth <= H2M_MAX_DEPTH + 2);
    CHECK(n->type == H2M_NODE_TEXT);
    h2m_document_free(&doc);
    free(decoded);
    h2m_buf_free(&html);

    /* Many unclosed formatting elements stay bounded. */
    h2m_buf fmt = H2M_BUF_INIT;
    for (int i = 0; i < 5000; i++)
        h2m_buf_printf(&fmt, "<b id=%d>x", i);
    h2m_buf_puts(&fmt, "</p><p>end");
    decoded = h2m_decode_input(fmt.data, fmt.len, H2M_ENC_UTF8, &len, NULL, NULL, 0);
    h2m_document_init(&doc);
    h2m_parse(&doc, decoded, len);
    CHECK(doc.root->first_child != NULL);
    h2m_document_free(&doc);
    free(decoded);
    h2m_buf_free(&fmt);
}

void suite_parser(void)
{
    test_basic();
    test_implied_end_tags();
    test_adoption_agency();
    test_tables();
    test_raw_text();
    test_foreign_and_misc();
    test_depth_limit();
}
