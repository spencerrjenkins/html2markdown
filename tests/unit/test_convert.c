/*
 * HTML -> Markdown conversion tests. These pin down the output format
 * element by element; the fixture and round-trip suites cover whole
 * documents.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "buf.h"
#include "html2md.h"
#include "testlib.h"

static char *conv_opts(const char *html, const h2m_options *o)
{
    char *md = NULL;
    size_t len = 0;
    h2m_status st = h2m_convert(html, strlen(html), o, &md, &len);
    if (st != H2M_OK)
        return NULL;
    CHECK_EQ_INT(strlen(md), len);
    return md;
}

#define CHECK_MD_OPTS(opts, html, expected)        \
    do {                                           \
        char *m_ = conv_opts(html, opts);          \
        CHECK_EQ_STR(expected, m_);                \
        h2m_free(m_);                              \
    } while (0)

#define CHECK_MD(html, expected) CHECK_MD_OPTS(NULL, html, expected)

static h2m_options defaults(void)
{
    h2m_options o;
    h2m_options_init(&o);
    return o;
}

static void test_paragraphs_and_whitespace(void)
{
    CHECK_MD("", "");
    CHECK_MD("   \n\t ", "");
    CHECK_MD("<p>Hello</p>", "Hello\n");
    CHECK_MD("<p>  Hello   \n  world  </p>", "Hello world\n");
    CHECK_MD("<p>a</p><p>b</p>", "a\n\nb\n");
    CHECK_MD("<div>a</div><div>b</div>", "a\n\nb\n");
    CHECK_MD("<div><div><p>deep</p></div></div>", "deep\n");
    CHECK_MD("a<div>b</div>c", "a\n\nb\n\nc\n");
    CHECK_MD("<p>a<span> b </span>c</p>", "a b c\n");
    CHECK_MD("<p></p><p> </p><p>&nbsp;</p><p>x</p>", "x\n");
    CHECK_MD("<p>x&nbsp;&nbsp;y</p>", "x y\n");
    CHECK_MD("<p>line<br>break</p>", "line\\\nbreak\n");
    CHECK_MD("<p>line<br> <br>para</p>", "line\n\npara\n");
    CHECK_MD("<p>trailing<br></p><p><br>leading</p>", "trailing\n\nleading\n");
    CHECK_MD("<p>soft\nwrap</p>", "soft wrap\n");
    CHECK_MD("<p>caf&eacute; &mdash; &#8220;q&#8221;</p>", "caf\xC3\xA9 \xE2\x80\x94 \xE2\x80\x9Cq\xE2\x80\x9D\n");

    h2m_options o = defaults();
    o.br_style = H2M_BR_SPACES;
    CHECK_MD_OPTS(&o, "<p>a<br>b</p>", "a  \nb\n");
    o = defaults();
    o.preserve_nbsp = true;
    CHECK_MD_OPTS(&o, "<p>10&nbsp;km</p>", "10\xC2\xA0km\n");
}

static void test_headings(void)
{
    CHECK_MD("<h1>Title</h1>", "# Title\n");
    CHECK_MD("<h3>  Spaced   out </h3>", "### Spaced out\n");
    CHECK_MD("<h6>six</h6>", "###### six\n");
    CHECK_MD("<h2>A <em>b</em> <a href=\"/x\">c</a></h2>", "## A *b* [c](/x)\n");
    CHECK_MD("<h2>Two<br>lines</h2>", "## Two lines\n");
    CHECK_MD("<h1></h1><p>x</p>", "x\n");
    CHECK_MD("<h2>C#</h2>", "## C#\n");
    CHECK_MD("<h2>Issue #</h2>", "## Issue \\#\n");
    CHECK_MD("<h2>#</h2>", "## \\#\n");
    CHECK_MD("<h1><div>Block</div><p>inside</p></h1>", "# Block\n\ninside\n");
    CHECK_MD("<h2><p>From an editor</p></h2>", "## From an editor\n");
    CHECK_MD("<h3>Unclosed<p>Body text</p><ul><li>item</li></ul>", "### Unclosed\n\nBody text\n\n- item\n");
    CHECK_MD("<h2 id=\"s\">Section<a class=\"headerlink\" href=\"#s\">\xC2\xB6</a></h2>", "## Section\n");
    CHECK_MD("<h2>Title <a href=\"#t\">#</a></h2>", "## Title\n");
    CHECK_MD("<a class=\"anchor\" aria-label=\"Permalink: X\" href=\"#x\"><svg></svg></a><h2>X</h2>", "## X\n");
    CHECK_MD("<p>a</p><h2>b</h2><p>c</p>", "a\n\n## b\n\nc\n");

    h2m_options o = defaults();
    o.heading_style = H2M_HEADING_SETEXT;
    CHECK_MD_OPTS(&o, "<h1>Title</h1><h2>Sub</h2><h3>Three</h3>", "Title\n=====\n\nSub\n---\n\n### Three\n");
    CHECK_MD_OPTS(&o, "<h2>- item</h2>", "\\- item\n-------\n");
}

static void test_emphasis(void)
{
    CHECK_MD("<p><em>a</em> <strong>b</strong> <del>c</del></p>", "*a* **b** ~~c~~\n");
    CHECK_MD("<p><i>i</i><b>b</b><s>s</s></p>", "*i*__b__~~s~~\n");
    CHECK_MD("<p>a<em> b </em>c</p>", "a *b* c\n");
    CHECK_MD("<p>a<em></em>b<strong> </strong>c</p>", "ab c\n");
    CHECK_MD("<p>in<em>tra</em>word</p>", "in*tra*word\n");
    CHECK_MD("<p><strong><em>both</em></strong></p>", "***both***\n");
    CHECK_MD("<p><em>a <em>nested</em> b</em></p>", "*a nested b*\n");
    CHECK_MD("<p><b><b>x</b></b></p>", "**x**\n");
    /* Flanking rules: "**Note:**Text" would not render as bold. */
    CHECK_MD("<p><strong>Note:</strong>Text</p>", "<strong>Note:</strong>Text\n");
    CHECK_MD("<p><strong>Note:</strong> Text</p>", "**Note:** Text\n");
    CHECK_MD("<p>x<strong>(y)</strong></p>", "x<strong>(y)</strong>\n");
    CHECK_MD("<p><em>a</em><em>b</em></p>", "*a*_b_\n");
    CHECK_MD("<p><em>a.</em><strong>b</strong></p>", "*a.*__b__\n");
    CHECK_MD("<p><em>line<br>two</em></p>", "*line\\\ntwo*\n");
    CHECK_MD("<strong><div>block</div></strong>", "block\n");
    CHECK_MD("<p><b style=\"font-weight:normal\">plain</b></p>", "plain\n");
    CHECK_MD("<p><span style=\"font-weight:700\">bold</span> "
             "<span style=\"font-style: italic\">it</span> "
             "<span style=\"text-decoration:line-through\">del</span></p>",
             "**bold** *it* ~~del~~\n");
    CHECK_MD("<p><cite>Book</cite> <var>x</var> <dfn>term</dfn></p>", "*Book* *x* *term*\n");
    CHECK_MD("<p><u>u</u> <ins>i</ins> <mark>m</mark> <small>s</small></p>", "u i m s\n");

    h2m_options o = defaults();
    o.emphasis = '_';
    o.strong = '_';
    CHECK_MD_OPTS(&o, "<p><em>a</em> <strong>b</strong></p>", "_a_ __b__\n");
    CHECK_MD_OPTS(&o, "<p>in<em>tra</em>word</p>", "in*tra*word\n");
    o = defaults();
    o.raw_html = false;
    CHECK_MD_OPTS(&o, "<p><strong>Note:</strong>Text</p>", "**Note:**Text\n");
    o = defaults();
    o.flavor = H2M_FLAVOR_COMMONMARK;
    CHECK_MD_OPTS(&o, "<p><del>gone</del></p>", "<del>gone</del>\n");
    o.raw_html = false;
    CHECK_MD_OPTS(&o, "<p><del>gone</del></p>", "gone\n");
}

static void test_escaping(void)
{
    CHECK_MD("<p>*not emphasis* _nor this_</p>", "\\*not emphasis\\* \\_nor this\\_\n");
    CHECK_MD("<p>snake_case_name and 2*3 and a * b</p>", "snake_case_name and 2\\*3 and a * b\n");
    CHECK_MD("<p>__init__</p>", "\\_\\_init\\_\\_\n");
    CHECK_MD("<p>`code` [link](x) ![img](y)</p>", "\\`code\\` \\[link](x) !\\[img](y)\n");
    CHECK_MD("<p># not a heading</p>", "\\# not a heading\n");
    CHECK_MD("<p>#hashtag and a # sign</p>", "#hashtag and a # sign\n");
    CHECK_MD("<p>&gt; not a quote</p>", "\\> not a quote\n");
    CHECK_MD("<p>- not a list</p>", "\\- not a list\n");
    CHECK_MD("<p>+ not a list</p>", "\\+ not a list\n");
    CHECK_MD("<p>* not a list</p>", "\\* not a list\n");
    CHECK_MD("<p>1. not a list</p>", "1\\. not a list\n");
    CHECK_MD("<p>2024. A year</p>", "2024\\. A year\n");
    CHECK_MD("<p>3.14 is pi</p>", "3.14 is pi\n");
    CHECK_MD("<p>1) also</p>", "1\\) also\n");
    CHECK_MD("<p>---</p>", "\\---\n");
    CHECK_MD("<p>===</p>", "\\===\n");
    CHECK_MD("<p>a<br>===</p>", "a\\\n\\===\n");
    CHECK_MD("<p>-flag --long</p>", "-flag --long\n");
    CHECK_MD("<p>a - b + c = d</p>", "a - b + c = d\n");
    CHECK_MD("<p>&lt;div&gt; and a &lt; b and &lt;3</p>", "\\<div> and a < b and <3\n");
    CHECK_MD("<p>&amp;copy; &amp;#65; AT&amp;T &amp;bogus;</p>", "\\&copy; \\&#65; AT&T &bogus;\n");
    CHECK_MD("<p>C:\\path\\*x \\</p>", "C:\\path\\\\\\*x \\\\\n");
    CHECK_MD("<p>~~not struck~~ ~ approx</p>", "\\~\\~not struck\\~\\~ ~ approx\n");
    CHECK_MD("<p>Wow!<a href=\"/x\">link</a></p>", "Wow\\![link](/x)\n");
    CHECK_MD("<p>a | b</p>", "a | b\n");
    CHECK_MD("<p>1<span>. split</span></p>", "1\\. split\n");
    CHECK_MD("<p>tab\there</p>", "tab here\n");
    CHECK_MD("<p>ctl\x01x</p>", "ctlx\n");

    h2m_options o = defaults();
    o.flavor = H2M_FLAVOR_COMMONMARK;
    CHECK_MD_OPTS(&o, "<p>~~not struck~~</p>", "\\~~not struck~~\n");
}

static void test_links(void)
{
    CHECK_MD("<a href=\"https://example.com\">Example</a>", "[Example](https://example.com)\n");
    CHECK_MD("<a href=\"/a\" title=\"T &quot;q&quot;\">x</a>", "[x](/a \"T \\\"q\\\"\")\n");
    CHECK_MD("<a href=\"https://example.com\">https://example.com</a>", "<https://example.com>\n");
    CHECK_MD("<a href=\"mailto:me@x.org\">me@x.org</a>", "<me@x.org>\n");
    CHECK_MD("<a href=\"mailto:me@x.org\">Mail me</a>", "[Mail me](mailto:me@x.org)\n");
    CHECK_MD("<a href=\"/a b(c\">sp</a>", "[sp](/a%20b\\(c)\n");
    CHECK_MD("<a href=\"/wiki/A_(b)\">p</a>", "[p](/wiki/A_(b))\n");
    CHECK_MD("<a href=\"\">empty</a>", "[empty](<>)\n");
    CHECK_MD("<a name=\"anchor\">text</a>", "text\n");
    CHECK_MD("<a href=\"javascript:void(0)\">js</a>", "js\n");
    CHECK_MD("<a href=\"#\" onclick=\"f()\">Copy</a>", "Copy\n");
    CHECK_MD("<a href=\"#sec\">jump</a>", "[jump](#sec)\n");
    CHECK_MD("<p>see <a href=\"/x\"> spaced </a>here</p>", "see [spaced](/x) here\n");
    CHECK_MD("<a href=\"/x\">a [b] c</a>", "[a \\[b\\] c](/x)\n");
    CHECK_MD("<a href=\"/x\"><img src=\"i.png\" alt=\"logo\"></a>", "[![logo](i.png)](/x)\n");
    CHECK_MD("<a href=\"/x\"><i class=\"icon\"></i></a>", "");
    CHECK_MD("<a href=\"/x\" aria-label=\"Close\"><svg></svg></a>", "[Close](/x)\n");
    CHECK_MD("<a href=\"/x\"><div><h3>Card</h3><p>desc</p></div></a>", "[Card desc](/x)\n");
    CHECK_MD("<a href=\"/x\">a<a href=\"/y\">b</a></a>", "[a](/x)[b](/y)\n");
    CHECK_MD("<p><a href=\"/x\">one<br>two</a></p>", "[one\\\ntwo](/x)\n");
    CHECK_MD("<p><code><a href=\"/api\">func()</a></code></p>", "[`func()`](/api)\n");

    h2m_options o = defaults();
    o.base_url = "https://example.com/docs/page.html";
    CHECK_MD_OPTS(&o, "<a href=\"other.html\">o</a> <img src=\"../i.png\" alt=\"\">",
                  "[o](https://example.com/docs/other.html) ![](https://example.com/i.png)\n");
    CHECK_MD_OPTS(&o, "<base href=\"/root/\"><a href=\"x\">x</a>", "[x](https://example.com/root/x)\n");

    o = defaults();
    o.link_style = H2M_LINKS_REFERENCE;
    CHECK_MD_OPTS(&o, "<p><a href=\"/a\">A</a>, <a href=\"/b\" title=\"B\">B</a>, <a href=\"/a\">again</a></p>",
                  "[A][1], [B][2], [again][1]\n\n[1]: /a\n[2]: /b \"B\"\n");
    o = defaults();
    o.link_style = H2M_LINKS_TEXT;
    CHECK_MD_OPTS(&o, "<p>go <a href=\"/a\">here</a></p>", "go here\n");
}

static void test_images(void)
{
    CHECK_MD("<img src=\"a.png\" alt=\"Alt text\">", "![Alt text](a.png)\n");
    CHECK_MD("<img src=\"a.png\" alt=\"A\" title=\"T\">", "![A](a.png \"T\")\n");
    CHECK_MD("<img src=\"a.png\">", "![](a.png)\n");
    CHECK_MD("<img alt=\"no src\">", "no src\n");
    CHECK_MD("<img src=\"t.gif\" width=\"1\" height=\"1\">", "");
    CHECK_MD("<img src=\"data:image/png;base64,AAAA\" alt=\"inline\">", "inline\n");
    CHECK_MD("<img src=\"data:image/gif;base64,R0\" data-src=\"real.jpg\" alt=\"lazy\">", "![lazy](real.jpg)\n");
    CHECK_MD("<img srcset=\"s.jpg 1x, l.jpg 2x\" alt=\"set\">", "![set](s.jpg)\n");
    CHECK_MD("<img src=\"a b.png\" alt=\"x]y\">", "![x\\]y](a%20b.png)\n");
    CHECK_MD("<picture><source srcset=\"a.webp\"><img src=\"a.jpg\" alt=\"p\"></picture>", "![p](a.jpg)\n");

    h2m_options o = defaults();
    o.image_style = H2M_IMAGES_ALT;
    CHECK_MD_OPTS(&o, "<p>x <img src=\"a.png\" alt=\"pic\"> y</p>", "x pic y\n");
    o.image_style = H2M_IMAGES_NONE;
    CHECK_MD_OPTS(&o, "<p>x <img src=\"a.png\" alt=\"pic\"> y</p>", "x y\n");
    o = defaults();
    o.keep_data_uris = true;
    CHECK_MD_OPTS(&o, "<img src=\"data:image/png;base64,AAAA\" alt=\"i\">", "![i](data:image/png;base64,AAAA)\n");
}

static void test_code(void)
{
    CHECK_MD("<p>Use <code>x = 1</code> now</p>", "Use `x = 1` now\n");
    CHECK_MD("<p><code>a`b</code></p>", "``a`b``\n");
    CHECK_MD("<p><code>`tick</code></p>", "`` `tick ``\n");
    CHECK_MD("<p><code> padded </code>x</p>", "`padded` x\n");
    CHECK_MD("<p><code>*not* &lt;b&gt;</code></p>", "`*not* <b>`\n");
    CHECK_MD("<p><code>multi\n  line</code></p>", "`multi line`\n");
    CHECK_MD("<p><code></code>x</p>", "x\n");
    CHECK_MD("<p><kbd>Ctrl</kbd>+<kbd>C</kbd></p>", "<kbd>Ctrl</kbd>+<kbd>C</kbd>\n");
    CHECK_MD("<pre>plain\n  indented\n</pre>", "```\nplain\n  indented\n```\n");
    CHECK_MD("<pre><code class=\"language-js\">let a = 1;</code></pre>", "```js\nlet a = 1;\n```\n");
    CHECK_MD("<pre class=\"lang-rust\"><code>fn main() {}</code></pre>", "```rust\nfn main() {}\n```\n");
    CHECK_MD("<div class=\"highlight highlight-source-python\"><pre>x = 1</pre></div>", "```python\nx = 1\n```\n");
    CHECK_MD("<pre><code>has ```fence``` inside\n```\n</code></pre>", "````\nhas ```fence``` inside\n```\n````\n");
    CHECK_MD("<pre>a<br>b &amp; &lt;c&gt;</pre>", "```\na\nb & <c>\n```\n");
    CHECK_MD("<pre><b>bold</b> <a href=\"/x\">link</a></pre>", "```\nbold link\n```\n");
    CHECK_MD("<pre>\n\n\n</pre><p>x</p>", "x\n");
    CHECK_MD("<pre>\ttab</pre>", "```\n\ttab\n```\n");
    CHECK_MD("<xmp><b>raw</b></xmp>", "```\n<b>raw</b>\n```\n");

    h2m_options o = defaults();
    o.code_style = H2M_CODE_INDENTED;
    CHECK_MD_OPTS(&o, "<pre>a\n\nb</pre>", "    a\n\n    b\n");
    CHECK_MD_OPTS(&o, "<ul><li>x</li></ul><pre>code</pre>", "- x\n\n```\ncode\n```\n");
    o = defaults();
    o.fence = '~';
    CHECK_MD_OPTS(&o, "<pre>x</pre>", "~~~\nx\n~~~\n");
    o = defaults();
    o.raw_html = false;
    CHECK_MD_OPTS(&o, "<p><kbd>K</kbd></p>", "`K`\n");
}

static void test_lists(void)
{
    CHECK_MD("<ul><li>a</li><li>b</li></ul>", "- a\n- b\n");
    CHECK_MD("<ol><li>a</li><li>b</li></ol>", "1. a\n2. b\n");
    CHECK_MD("<ol start=\"9\"><li>a</li><li>b</li></ol>", "9. a\n10. b\n");
    CHECK_MD("<ol reversed><li>a</li><li>b</li><li>c</li></ol>", "3. a\n2. b\n1. c\n");
    CHECK_MD("<ol><li>a</li><li value=\"7\">b</li><li>c</li></ol>", "1. a\n7. b\n8. c\n");
    CHECK_MD("<ul><li>a<ul><li>b<ul><li>c</li></ul></li></ul></li><li>d</li></ul>",
             "- a\n  - b\n    - c\n- d\n");
    CHECK_MD("<ol><li>a<ul><li>b</li></ul></li></ol>", "1. a\n   - b\n");
    CHECK_MD("<ul><li><p>p1</p><p>p2</p></li><li>x</li></ul>", "- p1\n\n  p2\n\n- x\n");
    CHECK_MD("<ul><li><p>only</p></li><li><p>paras</p></li></ul>", "- only\n- paras\n");
    CHECK_MD("<ul><li>a</li><li></li><li>c</li></ul>", "- a\n-\n- c\n");
    CHECK_MD("<ul><li>line<br>two</li></ul>", "- line\\\n  two\n");
    CHECK_MD("<ul><li><pre>code\nmore</pre></li></ul>", "- ```\n  code\n  more\n  ```\n");
    CHECK_MD("<ul><li><blockquote>q</blockquote></li></ul>", "- > q\n");
    CHECK_MD("<ul><li>- dash</li><li>1. num</li></ul>", "- \\- dash\n- 1\\. num\n");
    CHECK_MD("<ul><li>a</li><ul><li>stray nested</li></ul><li>b</li></ul>", "- a\n  - stray nested\n- b\n");
    CHECK_MD("<ul><li>a</li></ul><ul><li>b</li></ul>", "- a\n\n* b\n");
    CHECK_MD("<ol><li>a</li></ol><ol><li>b</li></ol>", "1. a\n\n1) b\n");
    CHECK_MD("<ul><li>x<ol start=\"3\"><li>y</li></ol></li></ul>", "- x\n\n  3. y\n");
    CHECK_MD("<ul><li><input type=\"checkbox\" checked> done</li><li><input type=\"checkbox\"> todo</li></ul>",
             "- [x] done\n- [ ] todo\n");
    CHECK_MD("<p>before</p><ul><li>item</li></ul><p>after</p>", "before\n\n- item\n\nafter\n");
    CHECK_MD("<menu><li>m</li></menu>", "- m\n");

    h2m_options o = defaults();
    o.bullet = '*';
    CHECK_MD_OPTS(&o, "<ul><li>a</li></ul>", "* a\n");
    o = defaults();
    o.flavor = H2M_FLAVOR_COMMONMARK;
    CHECK_MD_OPTS(&o, "<ul><li><input type=\"checkbox\" checked> done</li></ul>", "- done\n");
}

static void test_blockquotes(void)
{
    CHECK_MD("<blockquote>quote</blockquote>", "> quote\n");
    CHECK_MD("<blockquote><p>a</p><p>b</p></blockquote>", "> a\n>\n> b\n");
    CHECK_MD("<blockquote>outer<blockquote>inner</blockquote></blockquote>", "> outer\n>\n> > inner\n");
    CHECK_MD("<blockquote><ul><li>x</li></ul></blockquote>", "> - x\n");
    CHECK_MD("<blockquote><h2>Head</h2></blockquote>", "> ## Head\n");
    CHECK_MD("<blockquote><p># hash</p></blockquote>", "> \\# hash\n");
    CHECK_MD("<blockquote></blockquote><p>x</p>", "x\n");
}

static void test_tables(void)
{
    CHECK_MD("<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>",
             "| A   | B   |\n| --- | --- |\n| 1   | 2   |\n");
    CHECK_MD("<table><thead><tr><th align=left>L</th><th style=\"text-align:center\">C</th>"
             "<th align=right>R</th></tr></thead><tbody><tr><td>1</td><td>2</td><td>3</td></tr></tbody></table>",
             "| L   |  C  |   R |\n| :-- | :-: | --: |\n| 1   |  2  |   3 |\n");
    CHECK_MD("<table><tr><td>no</td><td>header</td></tr><tr><td>x</td><td>y</td></tr></table>",
             "| no  | header |\n| --- | ------ |\n| x   | y      |\n");
    CHECK_MD("<table><tr><th>a|b</th></tr><tr><td><code>c|d</code></td></tr></table>",
             "| a\\|b   |\n| ------ |\n| `c\\|d` |\n");
    CHECK_MD("<table><tr><th>H</th></tr><tr><td>line<br>two</td></tr><tr><td><p>p1</p><p>p2</p></td></tr></table>",
             "| H           |\n| ----------- |\n| line<br>two |\n| p1<br>p2    |\n");
    CHECK_MD("<table><tr><th>A</th><th>B</th><th>C</th></tr><tr><td colspan=2>wide</td><td>c</td></tr></table>",
             "| A    | B   | C   |\n| ---- | --- | --- |\n| wide |     | c   |\n");
    CHECK_MD("<table><tr><th>A</th><th>B</th></tr><tr><td rowspan=2>r</td><td>1</td></tr><tr><td>2</td></tr></table>",
             "| A   | B   |\n| --- | --- |\n| r   | 1   |\n|     | 2   |\n");
    CHECK_MD("<table><tr><th>A</th></tr><tr><td>1</td><td>extra</td></tr></table>",
             "| A   |       |\n| --- | ----- |\n| 1   | extra |\n");
    CHECK_MD("<table><caption>Cap</caption><tr><th>A</th></tr><tr><td>1</td></tr></table>",
             "Cap\n\n| A   |\n| --- |\n| 1   |\n");
    CHECK_MD("<table><tr><th>\xE4\xB8\xAD\xE6\x96\x87</th><th>x</th></tr><tr><td>1</td><td>2</td></tr></table>",
             "| \xE4\xB8\xAD\xE6\x96\x87 | x   |\n| ---- | --- |\n| 1    | 2   |\n");
    CHECK_MD("<table><tr><td>nav</td><td>bar</td></tr></table>", "nav\n\nbar\n");
    /* Layout tables become plain blocks. */
    CHECK_MD("<table><tr><td><p>Just one cell</p></td></tr></table>", "Just one cell\n");
    CHECK_MD("<table role=\"presentation\"><tr><td>a</td><td>b</td></tr></table>", "a\n\nb\n");
    CHECK_MD("<table><tr><td>outer<table><tr><td>in1</td><td>in2</td></tr></table></td><td>x</td></tr></table>",
             "outer\n\nin1\n\nin2\n\nx\n");
    CHECK_MD("<table></table><table><tr><td></td><td></td></tr></table><p>x</p>", "x\n");
    CHECK_MD("<table><tr><th>A</th><th></th><th>B</th></tr><tr><td>1</td><td></td><td>2</td></tr></table>",
             "| A   | B   |\n| --- | --- |\n| 1   | 2   |\n");
    CHECK_MD("<table><tr><td>Header area</td></tr><tr><td>Body area</td></tr></table>", "Header area\n\nBody area\n");

    h2m_options o = defaults();
    o.compact_tables = true;
    CHECK_MD_OPTS(&o, "<table><tr><th>Name</th><th>V</th></tr><tr><td>alpha</td><td>1</td></tr></table>",
                  "| Name | V |\n| --- | --- |\n| alpha | 1 |\n");
    o = defaults();
    o.raw_html = false;
    CHECK_MD_OPTS(&o, "<table><tr><th>H</th></tr><tr><td>a<br>b</td></tr></table>", "| H   |\n| --- |\n| a b |\n");
    o = defaults();
    o.flavor = H2M_FLAVOR_COMMONMARK;
    CHECK_MD_OPTS(&o, "<table class=x><tr><th>H</th></tr><tr><td><b>a</b>  &amp; b</td></tr></table>",
                  "<table><tr><th>H</th></tr><tr><td><b>a</b> &amp; b</td></tr></table>\n");
    o.raw_html = false;
    CHECK_MD_OPTS(&o, "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td><em>2</em></td></tr></table>",
                  "A | B\\\n1 | *2*\n");
}

static void test_misc_blocks(void)
{
    CHECK_MD("<p>a</p><hr><p>b</p>", "a\n\n---\n\nb\n");
    CHECK_MD("<dl><dt>Term</dt><dd>Definition</dd></dl>", "**Term**\n\nDefinition\n");
    CHECK_MD("<details><summary>More</summary><p>Hidden text</p></details>", "**More**\n\nHidden text\n");
    CHECK_MD("<figure><img src=\"f.png\" alt=\"F\"><figcaption>Caption</figcaption></figure>",
             "![F](f.png)\n\nCaption\n");
    CHECK_MD("<p>x<sup>2</sup> H<sub>2</sub>O</p>", "x<sup>2</sup> H<sub>2</sub>O\n");
    CHECK_MD("<p><q>quoted</q></p>", "\"quoted\"\n");
    CHECK_MD("<p><ruby>漢<rt>kan</rt></ruby> <ruby>字<rp>(</rp><rt>ji</rt><rp>)</rp></ruby></p>",
             "漢(kan) 字(ji)\n");
    CHECK_MD("<p>a<wbr>b</p>", "ab\n");
    CHECK_MD("<address>Addr</address>", "Addr\n");
    CHECK_MD("<video src=\"/v/movie.mp4\"></video>", "[movie.mp4](/v/movie.mp4)\n");
    CHECK_MD("<audio title=\"Song\"><source src=\"s.mp3\"></audio>", "[Song](s.mp3)\n");
    CHECK_MD("<math><semantics><mi>x</mi><annotation encoding=\"application/x-tex\">x^2</annotation></semantics></math>",
             "$x^2$\n");
    CHECK_MD("<math display=\"block\"><annotation encoding=\"application/x-tex\">\\sum x</annotation></math>",
             "$$\n\\sum x\n$$\n");
    CHECK_MD("<p><script type=\"math/tex\">a+b</script></p>", "$a+b$\n");
    CHECK_MD("<p>n = <span style=\"display:none\"><math><annotation encoding=\"application/x-tex\">n</annotation>"
             "</math></span><img aria-hidden=\"true\" src=\"n.svg\" alt=\"n\">.</p>", "n = $n$.\n");

    h2m_options o = defaults();
    o.raw_html = false;
    CHECK_MD_OPTS(&o, "<p>x<sup>2</sup></p>", "x2\n");
}

static void test_removal(void)
{
    CHECK_MD("<script>var x = '<p>no</p>';</script><style>p{}</style><p>yes</p>", "yes\n");
    CHECK_MD("<noscript>Enable JS</noscript><template><p>t</p></template><p>ok</p>", "ok\n");
    CHECK_MD("<form><input type=text><button>Go</button><select><option>o</option></select>"
             "<textarea>t</textarea><label>Name</label></form>", "Name\n");
    CHECK_MD("<p>a<span hidden>h</span><span aria-hidden=\"true\">i</span>"
             "<span style=\"display: none\">d</span><span style=\"visibility:hidden\">v</span>b</p>", "ab\n");
    CHECK_MD("<iframe src=\"x\"></iframe><canvas>c</canvas><svg><text>s</text></svg><p>k</p>", "k\n");
    CHECK_MD("<dialog><p>modal</p></dialog><dialog open><p>shown</p></dialog>", "shown\n");
    CHECK_MD("<p>x<!-- comment -->y</p>", "xy\n");
    CHECK_MD("<head><title>T</title><meta name=x></head><body>b</body>", "b\n");

    h2m_options o = defaults();
    o.keep_hidden = true;
    CHECK_MD_OPTS(&o, "<p>a<span hidden>h</span></p>", "ah\n");

    /* Accordion/disclosure widgets: the header is a heading inside a
     * button, the panel is hidden until it is expanded. */
    CHECK_MD("<button type=button><div><svg></svg><h2>Section</h2></div></button>"
             "<div hidden><p>Panel text</p></div>",
             "## Section\n");
    CHECK_MD_OPTS(&o, "<button type=button><h2>Section</h2></button><div hidden><p>Panel text</p></div>",
                  "## Section\n\nPanel text\n");
    CHECK_MD("<button type=button>Submit</button><p>x</p>", "x\n");
    CHECK_MD("<p>a<button>Copy</button>b</p>", "ab\n");
}

/* A <code> element split into lines with <br> is a code block written with
 * inline markup. */
static void test_inline_code_blocks(void)
{
    CHECK_MD("<p><code>one<br>two</code></p>", "```\none\ntwo\n```\n");
    CHECK_MD("<div><code class=\"language-sh\">ls<br>cd /</code></div>", "```sh\nls\ncd /\n```\n");
    CHECK_MD("<p><code>a<br><br>b<br></code></p>", "```\na\n\nb\n```\n");
    /* Still a span when it is part of running text or of a cell. */
    CHECK_MD("<p>Run <code>a<br>b</code> now</p>", "Run `a b` now\n");
    CHECK_MD("<table><tr><th>H</th></tr><tr><td><code>a<br>b</code></td></tr></table>",
             "| H     |\n| ----- |\n| `a b` |\n");
    CHECK_MD("<p><a href=\"/x\"><code>a<br>b</code></a></p>", "[`a b`](/x)\n");
    CHECK_MD("<p><code>single line</code></p>", "`single line`\n");
}

/* An empty result must explain itself rather than leaving the user with a
 * blank file. */
struct warn_capture {
    char msg[256];
    int count;
};

static void capture_warning(void *user_data, const char *message)
{
    struct warn_capture *w = user_data;
    snprintf(w->msg, sizeof w->msg, "%s", message);
    w->count++;
}

static char *convert_capturing(const char *html, h2m_options *o, struct warn_capture *w)
{
    w->msg[0] = '\0';
    w->count = 0;
    o->warning = capture_warning;
    o->warning_data = w;
    char *md = NULL;
    size_t len = 0;
    CHECK(h2m_convert(html, strlen(html), o, &md, &len) == H2M_OK);
    return md;
}

static void test_empty_output_warning(void)
{
    struct warn_capture w;
    h2m_options o = defaults();
    h2m_buf doc = {NULL, 0, 0};

    /* Everything inside a collapsed (hidden) panel. */
    h2m_buf_puts(&doc, "<div hidden><p>");
    for (int i = 0; i < 60; i++)
        h2m_buf_puts(&doc, "Lots of hidden body text that the reader wants. ");
    h2m_buf_puts(&doc, "</p></div>");
    char *md = convert_capturing(doc.data, &o, &w);
    CHECK_EQ_STR("", md);
    CHECK_EQ_INT(1, w.count);
    CHECK(strstr(w.msg, "the output is empty") != NULL);
    CHECK(strstr(w.msg, "hidden") != NULL);
    CHECK(strstr(w.msg, "--keep-hidden") != NULL);
    h2m_free(md);

    /* The same document converts silently once the panels are kept. */
    o.keep_hidden = true;
    md = convert_capturing(doc.data, &o, &w);
    CHECK(md && strlen(md) > 100);
    CHECK_EQ_INT(0, w.count);
    h2m_free(md);

    /* A selector that matches nothing (the text here is plain, so the
     * selector is the reason it is missing). */
    h2m_buf plain = {NULL, 0, 0};
    h2m_buf_puts(&plain, "<div><p>");
    for (int i = 0; i < 60; i++)
        h2m_buf_puts(&plain, "Ordinary visible body text in a div. ");
    h2m_buf_puts(&plain, "</p></div>");
    o = defaults();
    o.select = "article";
    md = convert_capturing(plain.data, &o, &w);
    CHECK_EQ_STR("", md);
    CHECK_EQ_INT(1, w.count);
    CHECK(strstr(w.msg, "--select") != NULL);
    h2m_free(md);
    h2m_buf_free(&plain);

    /* Text inside form controls. */
    h2m_buf ctrl = {NULL, 0, 0};
    h2m_buf_puts(&ctrl, "<form><textarea>");
    for (int i = 0; i < 60; i++)
        h2m_buf_puts(&ctrl, "text typed into a form control field. ");
    h2m_buf_puts(&ctrl, "</textarea></form>");
    o = defaults();
    md = convert_capturing(ctrl.data, &o, &w);
    CHECK_EQ_INT(1, w.count);
    CHECK(strstr(w.msg, "form controls") != NULL);
    h2m_free(md);
    h2m_buf_free(&ctrl);

    /* Ordinary documents stay quiet, including when chrome is dropped. */
    o = defaults();
    o.main_content = true;
    md = convert_capturing("<body><nav>Home About Contact Products Support</nav>"
                           "<main><p>The article body.</p></main></body>", &o, &w);
    CHECK_EQ_INT(0, w.count);
    h2m_free(md);
    h2m_buf_free(&doc);
}

static void test_selection(void)
{
    const char *page =
        "<html><body><header><nav><a href=\"/\">Home</a></nav><h1>Site</h1></header>"
        "<main><article><header><h1>Post</h1></header><p>Body <span class=ad>AD</span></p>"
        "<nav>toc</nav><aside>note</aside></article></main>"
        "<aside>sidebar</aside><footer>foot</footer></body></html>";
    h2m_options o = defaults();
    o.main_content = true;
    CHECK_MD_OPTS(&o, page, "# Post\n\nBody AD\n\nnote\n");
    o.exclude = ".ad";
    CHECK_MD_OPTS(&o, page, "# Post\n\nBody\n\nnote\n");
    o = defaults();
    o.select = "article p, footer";
    CHECK_MD_OPTS(&o, page, "Body AD\n\nfoot\n");
    o = defaults();
    o.select = "h1";
    CHECK_MD_OPTS(&o, page, "# Site\n\n# Post\n");

    /* A dominant article inside <main> wins; a title outside it keeps <main>. */
    o = defaults();
    o.main_content = true;
    CHECK_MD_OPTS(&o, "<main><div class=toolbar>Files: a b c</div><article><p>The long readme text that "
                      "dominates the page content by far.</p></article></main>",
                  "The long readme text that dominates the page content by far.\n");
    CHECK_MD_OPTS(&o, "<main><h1>Title</h1><article><p>The long body text that dominates the page.</p>"
                      "</article></main>",
                  "# Title\n\nThe long body text that dominates the page.\n");

    const char *no_main = "<body><nav>n</nav><header>h</header><div><p>content</p></div><footer>f</footer></body>";
    o = defaults();
    o.main_content = true;
    CHECK_MD_OPTS(&o, no_main, "content\n");
}

static void test_front_matter(void)
{
    h2m_options o = defaults();
    o.front_matter = true;
    CHECK_MD_OPTS(&o,
                  "<html lang=\"en\"><head><title> My \"Page\" </title>"
                  "<meta name=\"description\" content=\"Desc\"><meta name=\"author\" content=\"Ann\">"
                  "<link rel=\"canonical\" href=\"https://x.org/p\"></head><body><p>Hi</p></body></html>",
                  "---\ntitle: \"My \\\"Page\\\"\"\ndescription: \"Desc\"\nauthor: \"Ann\"\nlang: \"en\"\n"
                  "url: \"https://x.org/p\"\n---\n\nHi\n");
    CHECK_MD_OPTS(&o, "<p>x</p>", "---\n---\n\nx\n");
}

static void test_wrap(void)
{
    h2m_options o = defaults();
    o.wrap = 20;
    CHECK_MD_OPTS(&o, "<p>The quick brown fox jumps over the lazy dog again and again.</p>",
                  "The quick brown fox\njumps over the lazy\ndog again and again.\n");
    CHECK_MD_OPTS(&o, "<ul><li>The quick brown fox jumps over the lazy dog.</li></ul>",
                  "- The quick brown fox\n  jumps over the lazy\n  dog.\n");
    CHECK_MD_OPTS(&o, "<p>Values between 1 and 2. And more text here</p>",
                  "Values between 1\nand 2. And more text\nhere\n");
    CHECK_MD_OPTS(&o, "<p>aaaaaaaaaaaaaaaaaaaaaaaaa bbb</p>", "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbb\n");
    CHECK_MD_OPTS(&o, "<p>short - dash in the middle of text</p>", "short - dash in the\nmiddle of text\n");
    CHECK_MD_OPTS(&o, "<p>word word word word - x</p>", "word word word\nword - x\n");
    CHECK_MD_OPTS(&o, "<pre>a very long line of code that must not wrap</pre>",
                  "```\na very long line of code that must not wrap\n```\n");
    CHECK_MD_OPTS(&o, "<p><em>emphasis that spans past the width</em></p>",
                  "*emphasis that spans\npast the width*\n");
    CHECK_MD_OPTS(&o, "<p><a href=\"https://example.com/long\">link text is long here</a></p>",
                  "[link text is long\nhere](https://example.com/long)\n");
}

static void test_robustness(void)
{
    /* Misnested and unclosed markup still yields sensible Markdown. */
    CHECK_MD("<p><b>bold <i>both</b> italic</i></p>", "**bold *both*** *italic*\n");
    CHECK_MD("<ul><li>one<li>two</ul>", "- one\n- two\n");
    CHECK_MD("<p>unclosed <em>emphasis", "unclosed *emphasis*\n");
    CHECK_MD("<table><tr><td>a<td>b</table>", "a\n\nb\n");
    CHECK_MD("<<<>>> &&& </ >", "<<<>>> &&&\n");

    /* Very deep nesting does not crash. */
    char *deep = malloc(200000 * 5 + 16);
    size_t k = 0;
    for (int i = 0; i < 200000; i++) {
        memcpy(deep + k, "<div>", 5);
        k += 5;
    }
    memcpy(deep + k, "bottom", 7);
    CHECK_MD(deep, "bottom\n");
    k = 0;
    for (int i = 0; i < 200000; i++) {
        memcpy(deep + k, "<ul><li>", 8);
        k += 8;
        if (k > 200000 * 5 - 16)
            break;
    }
    memcpy(deep + k, "x", 2);
    char *md = NULL;
    size_t len;
    CHECK(h2m_convert(deep, strlen(deep), NULL, &md, &len) == H2M_OK);
    CHECK(md && strstr(md, "x") != NULL);
    h2m_free(md);
    free(deep);

    /* Resource guards: thousands of attributes, chained rowspans and very
     * deep block nesting stay fast and keep their content. */
    h2m_buf big = {NULL, 0, 0};
    h2m_buf_puts(&big, "<p");
    for (int i = 0; i < 20000; i++)
        h2m_buf_printf(&big, " a%d=%d", i, i);
    h2m_buf_puts(&big, ">attrs</p>");
    CHECK_MD(big.data, "attrs\n");
    h2m_buf_clear(&big);
    h2m_buf_puts(&big, "<table><tr><th>a</th><th>b</th></tr>");
    for (int i = 0; i < 3000; i++)
        h2m_buf_puts(&big, "<tr><td rowspan=5000>x</td><td>y</td></tr>");
    h2m_buf_puts(&big, "</table>");
    md = NULL;
    CHECK(h2m_convert(big.data, big.len, NULL, &md, &len) == H2M_OK);
    CHECK(md && strstr(md, "x") && strstr(md, "y"));
    h2m_free(md);
    h2m_buf_clear(&big);
    for (int i = 0; i < 200; i++)
        h2m_buf_puts(&big, "<blockquote>");
    h2m_buf_puts(&big, "q");
    md = NULL;
    CHECK(h2m_convert(big.data, big.len, NULL, &md, &len) == H2M_OK);
    CHECK_EQ_INT(64 * 2 + 2, len); /* 64 levels of "> " + "q\n" */
    h2m_free(md);
    h2m_buf_free(&big);

    /* Invalid option values are rejected. */
    h2m_options o = defaults();
    o.bullet = 'x';
    char err[128] = "";
    o.error_buffer = err;
    o.error_buffer_size = sizeof err;
    md = NULL;
    CHECK(h2m_convert("x", 1, &o, &md, &len) == H2M_ERR_INVALID_ARGUMENT);
    CHECK(md == NULL);
    CHECK(strstr(err, "bullet") != NULL);
    o = defaults();
    o.select = "div >";
    o.error_buffer = err;
    o.error_buffer_size = sizeof err;
    CHECK(h2m_convert("x", 1, &o, &md, &len) == H2M_ERR_SELECTOR);
    o = defaults();
    o.encoding = "klingon";
    CHECK(h2m_convert("x", 1, &o, &md, &len) == H2M_ERR_ENCODING);
    CHECK(h2m_convert(NULL, 5, NULL, &md, &len) == H2M_ERR_INVALID_ARGUMENT);
    CHECK(h2m_convert("x", 1, NULL, NULL, &len) == H2M_ERR_INVALID_ARGUMENT);
    CHECK(h2m_convert(NULL, 0, NULL, &md, &len) == H2M_OK);
    CHECK_EQ_STR("", md);
    h2m_free(md);

    CHECK_EQ_STR("1.0.0", h2m_version());
    CHECK_EQ_STR("invalid CSS selector", h2m_status_string(H2M_ERR_SELECTOR));
}

void suite_convert(void)
{
    test_paragraphs_and_whitespace();
    test_headings();
    test_emphasis();
    test_escaping();
    test_links();
    test_images();
    test_code();
    test_lists();
    test_blockquotes();
    test_tables();
    test_misc_blocks();
    test_removal();
    test_selection();
    test_inline_code_blocks();
    test_empty_output_warning();
    test_front_matter();
    test_wrap();
    test_robustness();
}
