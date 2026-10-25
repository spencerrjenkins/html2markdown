# Conversion reference

How each HTML element is converted with the default options (GitHub Flavored
Markdown, raw HTML allowed). Options that change an element's output are
noted. The unit tests in `tests/unit/test_convert.c` pin every rule below.

## Document structure and text

| HTML | Markdown |
| --- | --- |
| `<html>`, `<body>`, `<main>`, `<article>`, `<section>`, `<div>`, `<header>`, `<footer>`, `<nav>`, `<aside>`, `<address>`, `<center>`, `<figure>`, `<figcaption>`, `<fieldset>`, `<legend>`, `<search>`, `<form>`, `<dd>` | Block: content separated from its neighbours by a blank line |
| `<p>` | Paragraph (blank line before and after); empty paragraphs vanish |
| text | Whitespace runs collapse to one space; leading/trailing spaces of lines are removed; `&nbsp;` becomes a space (`--preserve-nbsp` keeps U+00A0) |
| `<br>` | Hard break `\` + newline (`--br-style=spaces`: two spaces); two consecutive breaks start a new paragraph; breaks at the start or end of a block are dropped |
| `<wbr>` | Nothing |
| `<hr>` | `---` (or `***` at the start of a `-` list item) |
| `<h1>`–`<h6>` | `#`–`######` on one line (`--heading-style=setext` underlines h1/h2); inner line breaks become spaces; a trailing ` #` is escaped; an unclosed heading is split at its first paragraph, list or table |
| `<blockquote>` | Every line prefixed with `> ` (`>` on blank lines); nests |
| unknown / custom elements, `<span>`, `<font>`, `<label>`, `<time>`, `<data>`, `<abbr>`, `<bdi>`, `<bdo>`, `<small>`, `<big>`, `<mark>`, `<u>`, `<ins>`, `<nobr>`, `<slot>`, `<picture>` | Content only |

## Inline formatting

| HTML | Markdown | Notes |
| --- | --- | --- |
| `<em>`, `<i>`, `<cite>`, `<dfn>`, `<var>` | `*text*` | `--emphasis=_` for `_text_` |
| `<strong>`, `<b>` | `**text**` | `--strong=_` for `__text__`; `font-weight: normal` cancels |
| `<del>`, `<s>`, `<strike>` | `~~text~~` | CommonMark flavor: `<del>text</del>` (plain text with `--no-html`) |
| `<span style="…">` | `**`, `*`, `~~` | from `font-weight: 600–900/bold`, `font-style: italic`, `text-decoration: line-through` (Google Docs exports) |
| nested same emphasis | single delimiter pair | `<em>a <em>b</em></em>` → `*a b*` |
| emphasis around block content | content only | Markdown emphasis cannot span blocks |
| `<code>`, `<tt>`, `<samp>` | `` `code` `` | fence longer than any backtick run inside; padded when content starts/ends with a backtick; whitespace collapsed; formatting inside is dropped |
| `<code>` split by `<br>`, alone in its block | fenced code block | only `<br>` starts a line (other whitespace collapses, as in any inline element); inside running text, a table cell or a link it stays a code span |
| `<code><a href>…</a></code>` | ``[`name`](url)`` | |
| `<kbd>` | `<kbd>K</kbd>` | `--no-html`: `` `K` `` |
| `<sup>`, `<sub>` | `<sup>2</sup>`, `<sub>2</sub>` | `--no-html`: plain text |
| `<q>` | `"text"` | |
| `<ruby>`/`<rt>`/`<rp>` | `漢(kan)` | parentheses from `<rp>` or added |

Emphasis whose delimiters would not be recognised by a CommonMark parser
(for example bold text ending in punctuation followed directly by a letter)
switches to the alternative delimiter or to `<em>`/`<strong>`/`<del>`; see
[DESIGN.md](DESIGN.md#the-markdown-sink).

## Links and media

| HTML | Markdown |
| --- | --- |
| `<a href="u">text</a>` | `[text](u)` |
| `<a href="u" title="t">` | `[text](u "t")` |
| text equal to an http(s)/ftp/mailto URL | `<https://example.com>` / `<me@example.com>` |
| `--links=reference` | `[text][1]` … `[1]: u "t"` at the end (duplicates share a number) |
| `--links=none` | text only |
| no `href`, `javascript:`, `vbscript:`, `data:` or bare `#` | text only |
| no text | `aria-label` or `title`, otherwise dropped |
| block content inside `<a>` | flattened into single-line link text |
| heading permalinks (`class="anchor"`, `headerlink`, `¶` inside headings) | dropped |
| `<img src alt title>` | `![alt](src "title")` |
| lazy images | source from `data-src`, `data-lazy-src`, `data-original`, `srcset` when `src` is missing or a `data:` placeholder |
| `data:` image sources | dropped (alt text kept) unless `--keep-data-uris` |
| 1×1 / 0×0 images | dropped (tracking pixels) |
| `--images=alt` / `--images=none` | alt text only / nothing |
| `<video>`, `<audio>` | `[title or file name](src)` |
| `<iframe>`, `<embed>`, `<object>`, `<canvas>`, `<svg>`, `<map>` | dropped |

URLs: tabs and newlines are removed, spaces and `<`, `>`, `\` are
percent-encoded, unbalanced parentheses are backslash-escaped, and relative
URLs are resolved against `--base-url` / `<base href>` (fragment-only links
such as `#section` stay relative).

## Lists

| HTML | Markdown |
| --- | --- |
| `<ul>`, `<menu>`, `<dir>` | `- item` (`--bullet=*` or `+`) |
| `<ol>` | `1. item`; `start`, `reversed` and `<li value>` are honoured |
| nested lists | indented by the width of the parent marker |
| `<li>` with several blocks | loose list (blank lines between items) |
| two adjacent lists | the second uses `*` (or `)` for ordered lists) so they stay separate |
| `<li><input type="checkbox" checked>` | `- [x] item` (GFM task list) |
| list nested directly in `<ul>` | attached to the preceding item |
| empty `<li>` | `-` |

## Code blocks

| HTML | Markdown |
| --- | --- |
| `<pre>`, `<pre><code>`, `<listing>`, `<xmp>`, `<plaintext>` | fenced block; text taken verbatim, `<br>` → newline, markup inside dropped |
| language | from `language-x`, `lang-x`, `highlight-source-x`, `highlight-x`, `brush: x`, `data-lang`, `data-language` on the code, the pre or two ancestors |
| content containing ```` ``` ```` | longer fence |
| `--code-style=indented` | four-space indentation (fenced right after a list, where indentation would continue the list) |
| `--fence=~` | `~~~` fences |
| leading blank lines, trailing blank lines | removed |

## Tables

| HTML | Markdown |
| --- | --- |
| `<table>` with rows and columns | GFM pipe table; first row is the header (a `<thead>` or `<th>` row when present) |
| `align` / `text-align` | `:--`, `:-:`, `--:` and matching cell padding |
| `\|` in cells | `\\|` (also inside code spans, as GFM requires) |
| several blocks or `<br>` in a cell | joined with `<br>` (`--no-html`: a space) |
| `colspan` | extra empty cells |
| `rowspan` | empty cells in the following rows |
| columns empty in every row | removed |
| `<caption>` | paragraph before the table |
| layout tables | rendered as normal blocks: `role="presentation"`/`none`, a single cell, a single header-less row or column, or a nested table |
| CommonMark flavor | minimal inline HTML table (`--no-html`: `a \| b` lines) |
| `--compact-tables` | no column padding |

## Definition lists and disclosure

| HTML | Markdown |
| --- | --- |
| `<dl>` | block |
| `<dt>` | `**term**` paragraph |
| `<dd>` | paragraph(s) |
| `<details>` | block |
| `<summary>` | `**summary**` paragraph |
| `<dialog open>` | block (without `open`: removed) |

## Math

| HTML | Markdown |
| --- | --- |
| MathML with `<annotation encoding="application/x-tex">` | `$tex$`; `display="block"` gives a `$$` block |
| `<script type="math/tex">` (MathJax 2) | `$tex$`; `mode=display` gives a `$$` block |
| MathML without TeX | `alttext`, otherwise the text content |

## Removed content

A `<button>` that wraps a heading is treated as a disclosure/accordion
header and its content is kept, because the heading is document structure
rather than a control label.

Always removed: `<head>` (its metadata feeds `--front-matter`), `<script>`,
`<style>`, `<noscript>`, `<template>`, `<link>`, `<meta>`, `<base>`,
`<title>` in the body, form controls (`<input>` except task-list checkboxes,
`<button>`, `<select>`, `<option>`, `<textarea>`, `<output>`, `<datalist>`,
`<meter>`, `<progress>`), frames, comments, processing instructions and
CDATA outside SVG/MathML.

Removed unless `--keep-hidden`: elements with the `hidden` attribute (except
`hidden="until-found"`), `aria-hidden="true"`, `display: none` or
`visibility: hidden` in their `style` attribute. Hidden MathML is kept, since
it is the accessible form of the equation shown next to it.

Collapsed accordions and disclosure panels (Headless UI, and similar widgets
that mark the closed panel with the `hidden` attribute) fall under this rule:
their text is not converted unless you pass `--keep-hidden`. When removing
hidden content leaves little or nothing to convert, html2md says so on
standard error instead of writing an empty file.

## Main content (`--main`)

1. The root is the first `<main>`, else the first `role="main"` element,
   else the only top-level `<article>`, else `<body>`.
2. If that root contains exactly one `<article>` holding at least 60 % of its
   text and there is no `<h1>` outside that article, the article becomes the
   root.
3. Inside the root, `<nav>`, `<search>`, `role="navigation"` and
   `role="search"` are removed. `<header>`, `<footer>`, `<aside>`,
   `role="banner"`, `role="contentinfo"` and `role="complementary"` are
   removed unless they are inside the chosen `<main>`/`<article>`.

## Front matter (`--front-matter`)

```yaml
---
title: "…"        # <title>, else og:title
description: "…"  # meta description, else og:description
author: "…"       # meta author
date: "…"         # article:published_time, else meta date
lang: "…"         # <html lang>
url: "…"          # <link rel="canonical">, else --base-url
---
```

Fields that are absent are omitted; values are double-quoted YAML strings.
