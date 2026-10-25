/*
 * Tests for buffers, UTF-8 helpers, entities, encodings and the tag table.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "buf.h"
#include "encoding.h"
#include "entities.h"
#include "tags.h"
#include "testlib.h"
#include "utf8.h"

static void test_buf(void)
{
    h2m_buf b = H2M_BUF_INIT;
    CHECK_EQ_STR("", h2m_buf_cstr(&b));
    h2m_buf_puts(&b, "hello");
    h2m_buf_putc(&b, ' ');
    h2m_buf_append(&b, "world!!", 5);
    CHECK_EQ_STR("hello world", h2m_buf_cstr(&b));
    h2m_buf_splice(&b, 0, 5, "goodbye", 7);
    CHECK_EQ_STR("goodbye world", b.data);
    h2m_buf_splice(&b, 7, 6, "", 0);
    CHECK_EQ_STR("goodbye", b.data);
    h2m_buf_splice(&b, 7, 0, " all", 4);
    CHECK_EQ_STR("goodbye all", b.data);
    h2m_buf_repeat(&b, '-', 3);
    CHECK_EQ_STR("goodbye all---", b.data);
    h2m_buf_truncate(&b, 4);
    CHECK_EQ_STR("good", b.data);
    h2m_buf_printf(&b, "%d-%s", 42, "x");
    CHECK_EQ_STR("good42-x", b.data);
    h2m_buf_puts(&b, "   \t");
    h2m_buf_rtrim_spaces(&b);
    CHECK_EQ_STR("good42-x", b.data);
    CHECK(h2m_buf_last(&b) == 'x');
    size_t len;
    char *p = h2m_buf_detach(&b, &len);
    CHECK_EQ_INT(8, len);
    CHECK_EQ_STR("good42-x", p);
    CHECK(b.data == NULL && b.len == 0);
    free(p);

    /* Growth across many appends. */
    for (int i = 0; i < 100000; i++)
        h2m_buf_putc(&b, 'a');
    CHECK_EQ_INT(100000, b.len);
    h2m_buf_free(&b);
}

static void test_utf8(void)
{
    char out[4];
    CHECK_EQ_INT(1, h2m_utf8_encode('A', out));
    CHECK_EQ_INT(2, h2m_utf8_encode(0xE9, out));
    CHECK_EQ_INT(3, h2m_utf8_encode(0x20AC, out));
    CHECK_EQ_INT(4, h2m_utf8_encode(0x1F600, out));
    CHECK_EQ_INT(3, h2m_utf8_encode(0xD800, out)); /* surrogate -> U+FFFD */

    size_t adv;
    CHECK_EQ_INT(0x20AC, h2m_utf8_decode("\xE2\x82\xAC", 3, &adv));
    CHECK_EQ_INT(3, adv);
    CHECK_EQ_INT(0xFFFD, h2m_utf8_decode("\xC0\xAF", 2, &adv)); /* overlong */
    CHECK_EQ_INT(1, adv);
    CHECK_EQ_INT(0xFFFD, h2m_utf8_decode("\xE2\x82", 2, &adv)); /* truncated */
    CHECK_EQ_INT(0xFFFD, h2m_utf8_decode("\xED\xA0\x80", 3, &adv)); /* surrogate */
    CHECK_EQ_INT(0x1F600, h2m_utf8_decode_last("ab\xF0\x9F\x98\x80", 6));
    CHECK_EQ_INT('b', h2m_utf8_decode_last("ab", 2));

    CHECK(h2m_utf8_valid("plain ascii", 11));
    CHECK(h2m_utf8_valid("caf\xC3\xA9", 5));
    CHECK(!h2m_utf8_valid("caf\xE9", 4));

    CHECK_EQ_INT(5, h2m_utf8_width("hello", 5));
    CHECK_EQ_INT(4, h2m_utf8_width("\xE4\xB8\xAD\xE6\x96\x87", 6)); /* two CJK ideographs */
    CHECK_EQ_INT(1, h2m_utf8_width("e\xCC\x81", 3));                /* e + combining acute */

    CHECK(h2m_cp_is_space(' ') && h2m_cp_is_space(0xA0) && h2m_cp_is_space(0x3000));
    CHECK(!h2m_cp_is_space('a'));
    CHECK(h2m_cp_is_punct('!') && h2m_cp_is_punct(0x201C) && h2m_cp_is_punct(0x3002));
    CHECK(!h2m_cp_is_punct('a') && !h2m_cp_is_punct(0xE9));
    CHECK(h2m_cp_is_alnum('z') && h2m_cp_is_alnum(0xE9) && !h2m_cp_is_alnum('_'));
}

static char *decode(const char *s, bool attr)
{
    h2m_buf b = H2M_BUF_INIT;
    h2m_decode_entities(s, strlen(s), attr, &b);
    return h2m_buf_detach(&b, NULL);
}

#define CHECK_DECODE(input, attr, expected)     \
    do {                                        \
        char *d_ = decode(input, attr);         \
        CHECK_EQ_STR(expected, d_);             \
        free(d_);                               \
    } while (0)

static void test_entities(void)
{
    CHECK_DECODE("&amp;&lt;&gt;&quot;&apos;", false, "&<>\"'");
    CHECK_DECODE("&copy; &eacute;", false, "\xC2\xA9 \xC3\xA9");
    CHECK_DECODE("&#65;&#x42;&#X43;", false, "ABC");
    CHECK_DECODE("&#128512;", false, "\xF0\x9F\x98\x80");
    CHECK_DECODE("&#0;", false, "\xEF\xBF\xBD");
    CHECK_DECODE("&#x110000;", false, "\xEF\xBF\xBD");
    CHECK_DECODE("&#xD800;", false, "\xEF\xBF\xBD");
    CHECK_DECODE("&#150;", false, "\xE2\x80\x93");   /* Windows-1252 en dash */
    CHECK_DECODE("&#99999999999;", false, "\xEF\xBF\xBD");
    CHECK_DECODE("&copy", false, "\xC2\xA9");        /* legacy, no semicolon */
    CHECK_DECODE("&notit;", false, "\xC2\xACit;");   /* longest legacy prefix */
    CHECK_DECODE("&notin;", false, "\xE2\x88\x89");
    CHECK_DECODE("&bogus; & &#; &#x;", false, "&bogus; & &#; &#x;");
    CHECK_DECODE("&NotEqualTilde;", false, "\xE2\x89\x82\xCC\xB8"); /* two code points */
    CHECK_DECODE("a?b&copy=1", true, "a?b&copy=1");  /* attribute rule */
    CHECK_DECODE("a?b&copyx", true, "a?b&copyx");
    CHECK_DECODE("a?b&copy;x", true, "a?b\xC2\xA9x");
    CHECK_DECODE("&amp=", false, "&=");

    unsigned cp1, cp2;
    CHECK(h2m_entity_lookup("hellip;", 7, &cp1, &cp2) && cp1 == 0x2026 && cp2 == 0);
    CHECK(!h2m_entity_lookup("hellip", 6, NULL, NULL)); /* not a legacy name */
}

static char *dec(const char *in, size_t len, h2m_encoding forced, h2m_encoding *used)
{
    size_t out_len;
    char warn[128];
    return h2m_decode_input(in, len, forced, &out_len, used, warn, sizeof warn);
}

static void test_encoding(void)
{
    h2m_encoding used;
    char *s;

    s = dec("a\r\nb\rc\n", 8, H2M_ENC_UNKNOWN, &used);
    CHECK_EQ_STR("a\nb\nc\n", s);
    CHECK(used == H2M_ENC_UTF8);
    free(s);

    s = dec("\xEF\xBB\xBFhi", 5, H2M_ENC_UNKNOWN, &used);
    CHECK_EQ_STR("hi", s);
    free(s);

    s = dec("a\0b", 3, H2M_ENC_UNKNOWN, &used); /* NUL removed */
    CHECK_EQ_STR("ab", s);
    free(s);

    s = dec("caf\xE9 \x93q\x94", 8, H2M_ENC_UNKNOWN, &used); /* invalid UTF-8 -> 1252 */
    CHECK(used == H2M_ENC_WINDOWS_1252);
    CHECK_EQ_STR("caf\xC3\xA9 \xE2\x80\x9Cq\xE2\x80\x9D", s);
    free(s);

    const char *meta = "<meta charset=\"iso-8859-15\"><p>\xA4</p>";
    s = dec(meta, strlen(meta), H2M_ENC_UNKNOWN, &used);
    CHECK(used == H2M_ENC_ISO_8859_15);
    CHECK(strstr(s, "\xE2\x82\xAC") != NULL); /* euro sign */
    free(s);

    const char *http_equiv = "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1252\">\x80";
    s = dec(http_equiv, strlen(http_equiv), H2M_ENC_UNKNOWN, &used);
    CHECK(used == H2M_ENC_WINDOWS_1252);
    free(s);

    s = dec("\xFF\xFEh\0i\0=\xD8\x00\xDE", 10, H2M_ENC_UNKNOWN, &used); /* UTF-16LE + surrogate pair */
    CHECK(used == H2M_ENC_UTF16LE);
    CHECK_EQ_STR("hi\xF0\x9F\x98\x80", s);
    free(s);

    s = dec("\xFE\xFF\0h\0i", 6, H2M_ENC_UNKNOWN, &used);
    CHECK(used == H2M_ENC_UTF16BE);
    CHECK_EQ_STR("hi", s);
    free(s);

    s = dec("\xE9", 1, H2M_ENC_UTF8, &used); /* forced UTF-8: replacement */
    CHECK_EQ_STR("\xEF\xBF\xBD", s);
    free(s);

    CHECK(h2m_encoding_lookup(" Latin1 ", 8) == H2M_ENC_WINDOWS_1252);
    CHECK(h2m_encoding_lookup("UTF8", 4) == H2M_ENC_UTF8);
    CHECK(h2m_encoding_lookup("shift_jis", 9) == H2M_ENC_UNKNOWN);

    char warn[128];
    size_t n;
    const char *sj = "<meta charset=shift_jis>x";
    s = h2m_decode_input(sj, strlen(sj), H2M_ENC_UNKNOWN, &n, &used, warn, sizeof warn);
    CHECK(strstr(warn, "shift_jis") != NULL);
    free(s);
}

static void test_tags(void)
{
    CHECK(h2m_tag_table_sorted());
    CHECK(h2m_tag_lookup("div", 3) == TAG_DIV);
    CHECK(h2m_tag_lookup("a", 1) == TAG_A);
    CHECK(h2m_tag_lookup("xmp", 3) == TAG_XMP);
    CHECK(h2m_tag_lookup("h6", 2) == TAG_H6);
    CHECK(h2m_tag_lookup("custom-element", 14) == TAG_UNKNOWN);
    CHECK_EQ_STR("blockquote", h2m_tag_name(TAG_BLOCKQUOTE));
    CHECK(h2m_tag_has(TAG_BR, TF_VOID));
    CHECK(h2m_tag_has(TAG_B, TF_FORMAT));
    CHECK(!h2m_tag_has(TAG_SPAN, TF_BLOCK));
    CHECK(h2m_tag_is_heading(TAG_H3) && !h2m_tag_is_heading(TAG_HR));
}

void suite_utils(void)
{
    test_buf();
    test_utf8();
    test_entities();
    test_encoding();
    test_tags();
}
