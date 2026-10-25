/*
 * Known HTML element names and their parsing / conversion categories.
 *
 * The list MUST stay sorted by name (bytewise); lookups use binary search and
 * a unit test verifies the ordering.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_TAGS_H
#define H2M_TAGS_H

#include <stdbool.h>
#include <stddef.h>

/* Parser categories (WHATWG HTML "tree construction"). */
#define TF_VOID       0x0001u /* void element: never has content */
#define TF_SPECIAL    0x0002u /* "special" category */
#define TF_FORMAT     0x0004u /* goes on the list of active formatting elements */
#define TF_CLOSES_P   0x0008u /* start tag closes a <p> in button scope */
#define TF_IMPLIED    0x0010u /* closed by "generate implied end tags" */
#define TF_SCOPE      0x0020u /* boundary for "has an element in scope" */
/* Conversion categories. */
#define TF_BLOCK      0x0100u /* block-level: separated from siblings by blank lines */
#define TF_DROP       0x0200u /* never contributes content to the Markdown output */

#define H2M_TAG_LIST(X)                                                              \
    X(A, "a", TF_FORMAT)                                                             \
    X(ABBR, "abbr", 0)                                                               \
    X(ACRONYM, "acronym", 0)                                                         \
    X(ADDRESS, "address", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                       \
    X(APPLET, "applet", TF_SPECIAL | TF_SCOPE | TF_DROP)                             \
    X(AREA, "area", TF_SPECIAL | TF_VOID | TF_DROP)                                  \
    X(ARTICLE, "article", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                       \
    X(ASIDE, "aside", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                           \
    X(AUDIO, "audio", 0)                                                             \
    X(B, "b", TF_FORMAT)                                                             \
    X(BASE, "base", TF_SPECIAL | TF_VOID | TF_DROP)                                  \
    X(BASEFONT, "basefont", TF_SPECIAL | TF_VOID | TF_DROP)                          \
    X(BDI, "bdi", 0)                                                                 \
    X(BDO, "bdo", 0)                                                                 \
    X(BGSOUND, "bgsound", TF_SPECIAL | TF_VOID | TF_DROP)                            \
    X(BIG, "big", TF_FORMAT)                                                         \
    X(BLINK, "blink", 0)                                                             \
    X(BLOCKQUOTE, "blockquote", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                 \
    X(BODY, "body", TF_SPECIAL | TF_BLOCK)                                           \
    X(BR, "br", TF_SPECIAL | TF_VOID)                                                \
    X(BUTTON, "button", TF_SPECIAL | TF_DROP)                                        \
    X(CANVAS, "canvas", TF_DROP)                                                     \
    X(CAPTION, "caption", TF_SPECIAL | TF_SCOPE | TF_BLOCK)                          \
    X(CENTER, "center", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                         \
    X(CITE, "cite", 0)                                                               \
    X(CODE, "code", TF_FORMAT)                                                       \
    X(COL, "col", TF_SPECIAL | TF_VOID | TF_DROP)                                    \
    X(COLGROUP, "colgroup", TF_SPECIAL | TF_DROP)                                    \
    X(DATA, "data", 0)                                                               \
    X(DATALIST, "datalist", TF_DROP)                                                 \
    X(DD, "dd", TF_SPECIAL | TF_IMPLIED | TF_BLOCK)                                  \
    X(DEL, "del", 0)                                                                 \
    X(DETAILS, "details", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                       \
    X(DFN, "dfn", 0)                                                                 \
    X(DIALOG, "dialog", TF_CLOSES_P | TF_BLOCK)                                      \
    X(DIR, "dir", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                               \
    X(DIV, "div", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                               \
    X(DL, "dl", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(DT, "dt", TF_SPECIAL | TF_IMPLIED | TF_BLOCK)                                  \
    X(EM, "em", TF_FORMAT)                                                           \
    X(EMBED, "embed", TF_SPECIAL | TF_VOID | TF_DROP)                                \
    X(FIELDSET, "fieldset", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                     \
    X(FIGCAPTION, "figcaption", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                 \
    X(FIGURE, "figure", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                         \
    X(FONT, "font", TF_FORMAT)                                                       \
    X(FOOTER, "footer", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                         \
    X(FORM, "form", TF_SPECIAL | TF_BLOCK)                                           \
    X(FRAME, "frame", TF_SPECIAL | TF_VOID | TF_DROP)                                \
    X(FRAMESET, "frameset", TF_SPECIAL | TF_DROP)                                    \
    X(H1, "h1", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(H2, "h2", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(H3, "h3", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(H4, "h4", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(H5, "h5", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(H6, "h6", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(HEAD, "head", TF_SPECIAL | TF_DROP)                                            \
    X(HEADER, "header", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                         \
    X(HGROUP, "hgroup", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                         \
    X(HR, "hr", TF_SPECIAL | TF_VOID | TF_BLOCK)                                     \
    X(HTML, "html", TF_SPECIAL | TF_SCOPE | TF_BLOCK)                                \
    X(I, "i", TF_FORMAT)                                                             \
    X(IFRAME, "iframe", TF_SPECIAL | TF_DROP)                                        \
    X(IMAGE, "image", TF_VOID)                                                       \
    X(IMG, "img", TF_SPECIAL | TF_VOID)                                              \
    X(INPUT, "input", TF_SPECIAL | TF_VOID)                                          \
    X(INS, "ins", 0)                                                                 \
    X(KBD, "kbd", 0)                                                                 \
    X(KEYGEN, "keygen", TF_SPECIAL | TF_VOID | TF_DROP)                              \
    X(LABEL, "label", 0)                                                             \
    X(LEGEND, "legend", TF_BLOCK)                                                    \
    X(LI, "li", TF_SPECIAL | TF_IMPLIED | TF_BLOCK)                                  \
    X(LINK, "link", TF_SPECIAL | TF_VOID | TF_DROP)                                  \
    X(LISTING, "listing", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                       \
    X(MAIN, "main", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                             \
    X(MAP, "map", TF_DROP)                                                           \
    X(MARK, "mark", 0)                                                               \
    X(MARQUEE, "marquee", TF_SPECIAL | TF_SCOPE | TF_BLOCK)                          \
    X(MATH, "math", 0)                                                               \
    X(MENU, "menu", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                             \
    X(META, "meta", TF_SPECIAL | TF_VOID | TF_DROP)                                  \
    X(METER, "meter", TF_DROP)                                                       \
    X(NAV, "nav", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                               \
    X(NOBR, "nobr", TF_FORMAT)                                                       \
    X(NOEMBED, "noembed", TF_SPECIAL | TF_DROP)                                      \
    X(NOFRAMES, "noframes", TF_SPECIAL | TF_DROP)                                    \
    X(NOSCRIPT, "noscript", TF_SPECIAL | TF_DROP)                                    \
    X(OBJECT, "object", TF_SPECIAL | TF_SCOPE | TF_DROP)                             \
    X(OL, "ol", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(OPTGROUP, "optgroup", TF_IMPLIED | TF_DROP)                                    \
    X(OPTION, "option", TF_IMPLIED | TF_DROP)                                        \
    X(OUTPUT, "output", TF_DROP)                                                     \
    X(P, "p", TF_SPECIAL | TF_CLOSES_P | TF_IMPLIED | TF_BLOCK)                      \
    X(PARAM, "param", TF_SPECIAL | TF_VOID | TF_DROP)                                \
    X(PICTURE, "picture", 0)                                                         \
    X(PLAINTEXT, "plaintext", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                   \
    X(PRE, "pre", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                               \
    X(PROGRESS, "progress", TF_DROP)                                                 \
    X(Q, "q", 0)                                                                     \
    X(RB, "rb", TF_IMPLIED)                                                          \
    X(RP, "rp", TF_IMPLIED)                                                          \
    X(RT, "rt", TF_IMPLIED)                                                          \
    X(RTC, "rtc", TF_IMPLIED)                                                        \
    X(RUBY, "ruby", 0)                                                               \
    X(S, "s", TF_FORMAT)                                                             \
    X(SAMP, "samp", 0)                                                               \
    X(SCRIPT, "script", TF_SPECIAL | TF_DROP)                                        \
    X(SEARCH, "search", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                         \
    X(SECTION, "section", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                       \
    X(SELECT, "select", TF_SPECIAL | TF_SCOPE | TF_DROP)                             \
    X(SLOT, "slot", 0)                                                               \
    X(SMALL, "small", TF_FORMAT)                                                     \
    X(SOURCE, "source", TF_SPECIAL | TF_VOID | TF_DROP)                              \
    X(SPAN, "span", 0)                                                               \
    X(STRIKE, "strike", TF_FORMAT)                                                   \
    X(STRONG, "strong", TF_FORMAT)                                                   \
    X(STYLE, "style", TF_SPECIAL | TF_DROP)                                          \
    X(SUB, "sub", 0)                                                                 \
    X(SUMMARY, "summary", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                       \
    X(SUP, "sup", 0)                                                                 \
    X(SVG, "svg", TF_DROP)                                                           \
    X(TABLE, "table", TF_SPECIAL | TF_SCOPE | TF_BLOCK)                              \
    X(TBODY, "tbody", TF_SPECIAL | TF_BLOCK)                                         \
    X(TD, "td", TF_SPECIAL | TF_SCOPE | TF_BLOCK)                                    \
    X(TEMPLATE, "template", TF_SPECIAL | TF_SCOPE | TF_DROP)                         \
    X(TEXTAREA, "textarea", TF_SPECIAL | TF_DROP)                                    \
    X(TFOOT, "tfoot", TF_SPECIAL | TF_BLOCK)                                         \
    X(TH, "th", TF_SPECIAL | TF_SCOPE | TF_BLOCK)                                    \
    X(THEAD, "thead", TF_SPECIAL | TF_BLOCK)                                         \
    X(TIME, "time", 0)                                                               \
    X(TITLE, "title", TF_SPECIAL | TF_DROP)                                          \
    X(TR, "tr", TF_SPECIAL | TF_BLOCK)                                               \
    X(TRACK, "track", TF_SPECIAL | TF_VOID | TF_DROP)                                \
    X(TT, "tt", TF_FORMAT)                                                           \
    X(U, "u", TF_FORMAT)                                                             \
    X(UL, "ul", TF_SPECIAL | TF_CLOSES_P | TF_BLOCK)                                 \
    X(VAR, "var", 0)                                                                 \
    X(VIDEO, "video", 0)                                                             \
    X(WBR, "wbr", TF_SPECIAL | TF_VOID)                                              \
    X(XMP, "xmp", TF_SPECIAL | TF_BLOCK)

typedef enum h2m_tag {
    TAG_UNKNOWN = 0,
#define H2M_TAG_ENUM(id, name, flags) TAG_##id,
    H2M_TAG_LIST(H2M_TAG_ENUM)
#undef H2M_TAG_ENUM
    TAG_COUNT_
} h2m_tag;

/* Looks up a lowercase tag name. Returns TAG_UNKNOWN when not listed. */
h2m_tag h2m_tag_lookup(const char *name, size_t len);

/* Canonical lowercase name ("" for TAG_UNKNOWN). */
const char *h2m_tag_name(h2m_tag tag);

/* Category flags (TF_*) for a tag. */
unsigned h2m_tag_flags(h2m_tag tag);

static inline bool h2m_tag_has(h2m_tag tag, unsigned flag)
{
    return (h2m_tag_flags(tag) & flag) != 0;
}

static inline bool h2m_tag_is_heading(h2m_tag tag)
{
    return tag >= TAG_H1 && tag <= TAG_H6;
}

/* Returns true when the table is sorted (used by the unit tests). */
bool h2m_tag_table_sorted(void);

#endif /* H2M_TAGS_H */
