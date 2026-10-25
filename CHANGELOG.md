# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 1.0.0 - 2026-09-22

First release.

### Added

- **HTML parser** following the WHATWG algorithm: tokenizer states for data,
  tags, attributes, comments, DOCTYPE, bogus comments, RCDATA, RAWTEXT,
  PLAINTEXT and CDATA; tree builder with implied end tags, scope rules, the
  special-element category, the adoption agency algorithm, reconstruction of
  active formatting elements, foster parenting, implied table rows and cells,
  and SVG/MathML foreign content. All 2,231 named character references,
  including the 106 legacy ones valid without a semicolon.
- **Input decoding** with BOM and `<meta charset>` sniffing for UTF-8,
  UTF-16LE/BE, Windows-1252/Latin-1 and ISO-8859-15, plus stream
  preprocessing (CRLF → LF, NUL removal, U+FFFD for invalid sequences).
- **Markdown generation** covering headings, paragraphs, line breaks,
  emphasis, strong, strikethrough, inline code, code blocks with language
  detection, links (inline, reference, autolink), images (including
  lazy-loading attributes), ordered/unordered/task lists, block quotes,
  tables with alignment and spans, definition lists, `<details>`, `<q>`,
  `<ruby>`, `<sup>`/`<sub>`/`<kbd>`, media links and TeX math from MathML,
  KaTeX and MathJax.
- **Context-aware escaping**: Markdown syntax characters are escaped only
  where a parser would misread them, with line-start tracking across text
  nodes and container prefixes.
- **Emphasis validation**: delimiters are checked against the CommonMark
  flanking rules and repaired (alternative delimiter or inline HTML) when
  they would not parse.
- **Content cleanup**: scripts, styles, forms, embedded objects, comments,
  hidden elements, tracking pixels, heading permalinks, empty table columns
  and layout tables are removed or unwrapped. Headings wrapped in a
  `<button>` (accordion headers) are kept, and a `<code>` element split by
  `<br>` becomes a fenced code block.
- **Diagnostics**: when a document that contains text converts to nothing,
  html2md reports how much text was removed, which rule removed it and which
  option recovers it, instead of writing an empty file silently.
- **Content selection**: `--main` heuristics and a CSS selector engine for
  `--select` / `--exclude`.
- **Output options**: GFM or CommonMark, ATX or setext headings, bullet,
  emphasis, strong, fence and line-break characters, fenced or indented code,
  inline/reference/no links, image modes, table padding, raw-HTML-free
  output, non-breaking space handling, paragraph wrapping and YAML front
  matter.
- **Command-line tool** with GNU-style options (abbreviations, clustering,
  `--opt=value`, `--`), multiple inputs, `-o`/`-d` output handling,
  overwrite protection, typo suggestions and documented exit statuses.
- **C library** (`libhtml2md.a`, `html2md.h`) with an options struct,
  diagnostics callback, error buffer and no global state.
- **Documentation**: README, man page, design document and a complete
  conversion reference.
- **Tests**: 830+ unit assertions, 22 golden-file documents, end-to-end CLI
  tests, a CommonMark spec round-trip harness (652 examples × 15 option
  combinations) against cmark-gfm, a differential round-trip fuzzer, a
  mutation fuzzer with output invariants, and sanitizer builds.
