# Design

This document explains how html2md works and why it is built the way it is.
It is meant for contributors and for anyone evaluating whether the output can
be trusted. For the element-by-element output reference see
[CONVERSION.md](CONVERSION.md).

## Goals

1. **Correct Markdown.** Output must mean what the HTML meant when rendered
   by a conforming CommonMark/GFM parser. Text that merely *looks* like
   Markdown syntax must stay literal; generated syntax must actually parse.
2. **Clean Markdown.** Escape only where needed, collapse whitespace the way
   browsers do, drop what is not content, and produce predictable, idiomatic
   formatting (ATX headings, `-` bullets, fenced code, aligned tables).
3. **Robust on real-world HTML.** Malformed markup must convert the way a
   browser would interpret it, and no input may crash the program, hang it,
   or consume unbounded memory.
4. **Dependency-free, portable C.** C11 (also builds as C99/C17), standard
   library only, usable as a CLI and as a library.

Non-goals: executing JavaScript, applying CSS stylesheets (inline `style`
attributes are consulted for a few properties), and "readability"-style
article scoring beyond the simple, documented `--main` rules.

## Research summary

Before implementation the following sources were studied; the findings below
directly shaped the design.

| Source | Finding that shaped html2md |
| --- | --- |
| [WHATWG HTML parsing algorithm](https://html.spec.whatwg.org/multipage/parsing.html) | Real pages depend on browser error recovery: implied end tags, scope rules, the "special" element category, the adoption agency algorithm for misnested formatting, reconstruction of active formatting elements, foster parenting of stray table content, raw-text elements, and 2,231 named character references (106 of them valid without `;`). A naive tag-matching parser produces wrong structure on common input such as `<p>a<div>b</div>` or `<b>1<p>2</b>3`. |
| [CommonMark 0.31.2](https://spec.commonmark.org/0.31.2/) | Emphasis only parses when delimiter runs are *left-/right-flanking*; `**Note:**Text` is **not** bold. `_` does not work inside words. Line-start constructs (`#`, `>`, `-`, `+`, `*`, `1.`, `===`, `---`, fences) and inline constructs (`` ` ``, `[`, `<`, `&entity;`, `\`) need escaping only in specific positions. An ordered list can interrupt a paragraph only when it starts at 1. |
| [GFM spec](https://github.github.com/gfm/) | Table cells cannot contain block content, and `\\|` must be escaped *even inside code spans*. Single and double `~` both create strikethrough. Task list items are `[ ]`/`[x]` at the start of an item. |
| [turndown](https://github.com/mixmark-io/turndown) | Robust separator logic: blocks request newlines and adjacent requests collapse (like CSS margins) rather than emitting and later trimming. Leading/trailing whitespace inside inline elements must move *outside* the delimiters ("flanking whitespace"). Conversely, its escaping is intentionally over-eager, which hurts readability. |
| [html-to-markdown v2](https://github.com/JohannesKaufmann/html-to-markdown) | "Smart" escaping (escape only when necessary) is worth the complexity; CSS-selector include/exclude is the expected CLI interface for content selection. |
| html2text / pandoc | Conventions users expect: fenced code with language from `language-*` classes, reference-style links as an option, wrapping as an option, YAML front matter for metadata. |

Two conclusions followed. First, parsing had to be browser-grade, so html2md
contains a real HTML5-style tree builder instead of a regex or tag-soup
parser. Second, escaping and emphasis had to be *context-aware*: the output
writer knows where each character will land (line start, inside a word,
inside link text) and decides per character.

## Pipeline

```
bytes ──► encoding.c ──► parser.c ──► DOM ──► convert.c ──► sink.c ──► Markdown
          decode to       tokenizer +          element       whitespace,
          UTF-8, CRLF→LF  tree builder         rules         escaping, wrap
```

1. **Decode** (`encoding.c`): byte-order mark, then `--encoding`, then a
   `<meta charset>` prescan of the first 1024 bytes, then "valid UTF-8, else
   Windows-1252". Supports UTF-8, Windows-1252/Latin-1/ASCII, ISO-8859-15 and
   UTF-16LE/BE. Invalid sequences become U+FFFD, CR/CRLF become LF, NUL is
   removed.
2. **Parse** (`parser.c`): tokenizer states for data, tags, attributes,
   comments, DOCTYPE, bogus comments, RCDATA, RAWTEXT, PLAINTEXT and CDATA
   (in SVG/MathML); a tree builder implementing the "in body" insertion mode
   closely (see below). The result is a DOM of `h2m_node`s in an arena.
3. **Prepare** (`convert.c`): mark removed nodes (hidden elements,
   `--exclude` matches), resolve the effective base URL, choose the root(s)
   (`--select`, `--main`, or `<body>`).
4. **Render** (`convert.c` + `sink.c`): a recursive walk; every element
   renders into an `h2m_sink`.
5. **Assemble**: optional YAML front matter, the body, reference
   definitions, exactly one trailing newline.

## Module map

| File | Responsibility |
| --- | --- |
| `include/html2md.h` | Public API: options, `h2m_convert`, status codes |
| `src/html2md.c` | Option validation and the decode → parse → convert pipeline |
| `src/encoding.c` | Encoding sniffing and transcoding, input preprocessing |
| `src/entities.c`, `src/entities_data.h` | Character references; table generated from WHATWG `entities.json` by `tools/gen_entities.py` |
| `src/parser.c` | Tokenizer and tree builder |
| `src/tags.c`, `src/tags.h` | Tag table (single X-macro list) with parser and converter categories |
| `src/dom.c` | Nodes, attributes, traversal |
| `src/selector.c` | CSS selector parser and matcher for `--select`/`--exclude` |
| `src/url.c` | URL cleanup and RFC 3986 reference resolution |
| `src/sink.c` | Markdown writer: whitespace, separators, escaping, emphasis repair, wrapping |
| `src/convert.c` | Element conversion rules, tables, lists, main-content selection, front matter |
| `src/cli.c`, `src/main.c` | Argument parsing (testable in isolation) and file handling |
| `src/buf.c`, `src/arena.c`, `src/utf8.c` | Growable buffers, arena allocator, UTF-8 and Unicode classification |

## The tree builder

The tree builder follows the WHATWG "in body" rules:

- **Implied end tags and scope.** `<p>` is closed by block-level start tags
  in *button scope*; `<li>`, `<dd>`, `<dt>` close their open siblings up to
  the nearest special element; end tags only close elements *in scope*, and
  "any other end tag" stops at special elements, so `</span>` can never close
  a `<div>`.
- **Active formatting elements.** Formatting elements (`b`, `i`, `a`, `em`,
  `font`, …) are recorded, re-opened after being implicitly closed, and
  misnesting is repaired by the **adoption agency algorithm**. For example
  `<b>1<p>2</b>3</p>` becomes `<b>1</b><p><b>2</b>3</p>`, exactly as in
  browsers. The Noah's Ark clause limits identical entries to three.
- **Tables.** Cells and rows are implied (`<table><td>` works), cells close
  each other, and text or elements that are not allowed inside table
  structure are **foster-parented** before the table.
- **Foreign content.** `<svg>` and `<math>` subtrees get their namespace,
  honour self-closing tags and CDATA, and "break out" on HTML tags as
  specified.
- **Raw text.** `script`, `style`, `xmp`, `iframe`, `noembed`, `noframes`,
  `noscript` (RAWTEXT) and `title`, `textarea` (RCDATA) end only at their
  matching end tag; the newline right after `<pre>`, `<listing>` and
  `<textarea>` is dropped.

Simplifications, all irrelevant to conversion: no `<tbody>` is synthesised,
script data is treated as RAWTEXT, and document-level modes (initial, in
head, after head) are condensed. Everything the tree builder does is covered
by html5lib-style tree tests in `tests/unit/test_parser.c`.

## The Markdown sink

`h2m_sink` is where output correctness lives. Each container that needs its
content post-processed (list item, block quote, emphasis, link text, heading,
table cell) renders into its own child sink.

**Deferred separators.** Nothing is written eagerly. Blocks call
`h2m_sink_block(s, 2)`, inline whitespace calls `h2m_sink_space`, `<br>`
calls `h2m_sink_br`; these only record *requests*. When content arrives the
strongest pending request is materialised: a blank line beats a line break
beats a space. Requests before any content, or after the last content, are
dropped. This gives browser-like whitespace collapsing and CSS-margin-like
block separation without any trimming pass. Two consecutive `<br>`s become a
paragraph break.

**Flanking whitespace.** Inline containers report whitespace seen before
their first and after their last content (`leading_space`,
`pending_space`); the parent emits it *outside* the delimiters, so
`a<em> b </em>c` becomes `a *b* c`.

**Context-sensitive escaping.** Each character of text is escaped only where
it could be misread:

| Character | Escaped when |
| --- | --- |
| `\` | followed by ASCII punctuation, or at the end of a text node |
| `` ` ``, `[` | always |
| `]` | inside link text or image alt text |
| `*` | unless surrounded by whitespace (and never at line start) |
| `_` | unless between two alphanumerics (intraword, e.g. `snake_case`) |
| `~` | GFM: unless surrounded by whitespace; CommonMark: at line start |
| `<` | followed by a letter, `/`, `!` or `?` (possible HTML or autolink) |
| `&` | when it starts a valid character reference such as `&copy;` |
| `#` | at line start, when a run of at most six is followed by a space |
| `>` | at line start |
| `-`, `+` | at line start, when alone or (for `-`) a run like `---` |
| `=` | at line start, when the word is all `=` (setext underline) |
| `.`, `)` | after 1–9 digits at line start and before a space (`1.`, `2024.`) |
| `!` | before a generated link, so `Wow![link]` does not become an image |

"Line start" is tracked across text nodes (for example `1<span>. x</span>`)
and across the prefixes that containers add later (list markers, `> `).

**Emphasis repair.** Before writing `**`/`*`/`~~` the sink checks the
CommonMark flanking rules with the actual neighbouring characters, looking
through adjacent delimiter runs of the same character (which merge). The
character *after* the closing delimiter is not known yet, so the check is
completed lazily when the next content arrives. If the preferred delimiter
would not parse, the sink rewrites the pair in place to the alternative
delimiter (`_`/`__`) or, as a last resort, to `<em>`/`<strong>`/`<del>`:

```
<strong>Note:</strong>Text   →  <strong>Note:</strong>Text   (** would not parse)
<strong>Note:</strong> Text  →  **Note:** Text
<em>a</em><em>b</em>         →  *a*_b_                       (** would merge)
```

**Wrapping.** With `--wrap`, every collapsible space written by the sink is
recorded as a candidate break. When a paragraph ends, a greedy pass replaces
candidate spaces with newlines, measuring display width (East Asian wide
characters count as two columns). A break is never placed where the next
line would start a block construct (`- `, `1.`, `#`, `>`, `===`, fences),
nor after an odd number of backslashes (which would create a hard break).
Code, link destinations and headings are never wrapped.

## Conversion highlights

- **Lists.** Items render into child sinks and are indented by the marker
  width, so any block (code, quotes, tables, nested lists) nests correctly.
  A list is loose only if an item contains blocks separated by a blank line.
  A nested list follows its parent item's text on the next line unless it
  could not interrupt a paragraph there (an ordered list not starting at 1).
  Consecutive lists would merge into one, so the marker alternates (`-`/`*`,
  `.`/`)`). An `<hr>` that starts a `-` item is written as `***`.
- **Code.** Inline code picks a backtick fence longer than any run inside
  and pads when needed; fenced blocks use a fence longer than any fence-like
  line in the content. Languages come from `language-*`, `lang-*`,
  `highlight-source-*`, `data-lang` and similar conventions on the `<code>`,
  the `<pre>` or up to two ancestors (GitHub, Sphinx, Jekyll, Prism,
  highlight.js markup all work).
- **Tables.** GFM tables with alignment, width-aware padding, colspan and
  rowspan expansion, `\|` escaping, `<br>` for multi-block cells, and removal
  of all-empty columns. Tables that are page layout (role="presentation", a
  single cell, a single header-less row or column, or nested tables) are
  rendered as ordinary blocks. Without GFM, tables become inline HTML (or
  plain `a | b` lines with `--no-html`).
- **Links and images.** Destinations with spaces or unbalanced parentheses
  are percent-encoded or escaped; `javascript:` and bare `#` links keep only
  their text; icon-only links fall back to `aria-label`/`title`; heading
  permalink anchors are dropped; `<https://…>` autolinks are used when the
  text equals the URL. Images follow lazy-loading attributes (`data-src`,
  `srcset`), skip `data:` URIs and 1×1 tracking pixels.
- **Headings** are single-line; an unclosed heading that swallows following
  paragraphs is split at its first content block. A heading wrapped in a
  `<button>` (an accordion header) is kept, unlike other control text.
- **Empty results explain themselves.** If a document with text converts to
  nothing (or nearly nothing), the converter reports how much text was
  removed, which rule removed it (hidden elements, `--exclude`, `--select`,
  `--main`, form controls) and which option recovers it. Silently writing an
  empty file is the one failure mode users cannot debug.
- **Math.** TeX annotations in MathML (KaTeX, MediaWiki) and MathJax
  `<script type="math/tex">` become `$…$` / `$$…$$`; MathML that is hidden
  from sighted readers is still used, as it is the only machine-readable form.

## Resource limits

Every input converts in time roughly linear in its size. The guards:

| Limit | Value | Effect when exceeded |
| --- | --- | --- |
| Element nesting depth | 512 | deeper elements become siblings (as in Chromium) |
| Active formatting elements | 256 per marker | further formatting is not reconstructed |
| Attributes per element | 256 | the rest are parsed and ignored |
| Nested lists/block quotes | 64 | deeper containers render without further indentation |
| Render recursion | 1024 | deeper subtrees are emitted as plain text |
| Table columns / cells | 1,000 / 1,000,000 | table rendered as plain blocks |
| `rowspan` | rows remaining in the table | clamped |

Small internal allocations abort the process on failure (the same policy as
cmark); the input-sized decode buffer is checked and reported as
`H2M_ERR_NOMEM`.

## Testing strategy

| Suite | What it proves |
| --- | --- |
| `tests/unit` (C) | 830+ assertions: buffers, UTF-8, entities, encodings, html5lib-style tree construction, selectors, RFC 3986 resolution, every conversion rule, CLI parsing |
| `tests/fixtures` | 22 realistic documents (blog, docs, Wikipedia, GitHub, Google Docs export, email newsletter, malformed markup, legacy encoding, CJK/RTL, math) with reviewed expected output |
| `tests/run_cli_tests.sh` | Exit statuses, stdin/stdout, `-o`, `-d`, error handling, overwrite protection |
| `tests/roundtrip.py` | All 652 CommonMark spec examples: html2md output rendered by cmark-gfm must be semantically equal to the original HTML and a fixed point of html2md |
| `tests/roundtrip_fuzz.py` | Differential fuzzing: random mutations, same fixed-point check against cmark-gfm |
| `tests/fuzz` | Mutation fuzzer / libFuzzer target under ASan+UBSan with output invariants |
| `make sanitize` | All suites under AddressSanitizer and UndefinedBehaviorSanitizer |

## Known limitations

- CSS stylesheets are not applied; content hidden only by a stylesheet (for
  example collapsed dropdown menus) is kept. Use `--exclude` for such chrome.
- Encodings other than UTF-8, UTF-16 and the Latin-1 family are decoded as
  UTF-8 with a warning.
- Markdown cannot represent every HTML construct: colspan/rowspan are
  approximated, block content in table cells is joined with `<br>`, and
  definition lists become bold terms followed by paragraphs.
