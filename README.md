# html2md

**Convert HTML to clean, correct Markdown.** html2md is a fast, dependency-free
command-line tool and C library. It parses HTML the way browsers do, removes
everything that isn't content, and writes GitHub Flavored Markdown or strict
CommonMark that renders exactly as the original HTML meant.

```console
$ curl -s https://example.com/blog/post | html2md --main --base-url https://example.com/blog/post
```

## Highlights

- **Browser-grade parsing.** A WHATWG-style tokenizer and tree builder
  (implied end tags, adoption agency, foster parenting, all 2,231 named
  character references), so broken real-world markup converts sensibly.
- **Correct output, minimal escaping.** Markdown characters are escaped only
  where a parser would otherwise misread them. Emphasis is checked against
  the CommonMark flanking rules and repaired when a delimiter would not parse.
  Verified by round-tripping all 652 CommonMark spec examples through
  cmark-gfm, GitHub's reference renderer.
- **Removes the clutter.** Scripts, styles, forms, hidden elements, tracking
  pixels, heading permalinks and layout tables are removed or unwrapped.
  `--main` keeps only the article; CSS selectors (`--select`, `--exclude`)
  give precise control.
- **Covers common HTML.** Headings, paragraphs, emphasis, links (inline or
  reference), images (including lazy-loaded ones), nested and task lists,
  code blocks with language detection, GFM tables with alignment and spans,
  block quotes, definition lists, `<details>`, `<sup>`/`<sub>`/`<kbd>`, and
  math (KaTeX, MathJax, MathML → `$TeX$`).
- **Real-world encodings.** UTF-8, UTF-16 and Windows-1252/Latin-1, detected
  from a BOM or `<meta charset>`.
- **Configurable.** Heading, bullet, emphasis, code and line-break styles;
  paragraph wrapping; YAML front matter; CommonMark-only output.
- **Fast and safe.** A 1 MB manual converts in about 20 ms. Output is linear
  in input size, with guards against pathological input. Tested under
  AddressSanitizer and UBSan and with fuzzing.

## Example

<table>
<tr><th>Input</th><th>html2md --main</th></tr>
<tr><td>

```html
<nav><a href="/">Home</a> · <a href="/blog">Blog</a></nav>
<main>
  <h1>Release 2.0 <a class="anchor" href="#release">¶</a></h1>
  <p>This release is <b>2× faster</b>.<strong>Note:</strong>the
  config format changed (see <a href="/docs/config">the docs</a>).</p>
  <ul><li>New <code>--turbo</code> flag<li>Fixed <em>many</em> bugs</ul>
  <pre><code class="language-sh">app --turbo *.txt</code></pre>
  <table><tr><th>Mode</th><th align="right">Time</th></tr>
    <tr><td>normal</td><td align="right">1.2 s</td></tr>
    <tr><td>turbo</td><td align="right">0.6 s</td></tr></table>
  <p>Prices from $5 * 2 = $10 — literal *stars* stay literal.</p>
</main>
<footer>© 2026 · <a href="/privacy">Privacy</a></footer>
```

</td><td>

````markdown
# Release 2.0

This release is **2× faster**.<strong>Note:</strong>the config format changed (see [the docs](https://example.com/docs/config)).

- New `--turbo` flag
- Fixed *many* bugs

```sh
app --turbo *.txt
```

| Mode   |  Time |
| ------ | ----: |
| normal | 1.2 s |
| turbo  | 0.6 s |

Prices from $5 * 2 = $10 — literal \*stars\* stay literal.
````

</td></tr>
</table>

(With `--base-url https://example.com/`.) Things to notice:

- The navigation, footer and `¶` permalink are gone.
- The unclosed `<li>` was handled.
- The code language came from the class name.
- Only the `*` characters that could form emphasis were escaped.
- `**Note:**the` would not render as bold in any CommonMark renderer, so
  html2md used `<strong>` for that one span.

## Installation

html2md needs only a C11 compiler (it also builds as C99 or C17) and `make`
or CMake. It has no runtime dependencies.

```sh
make                        # builds build/html2md and build/libhtml2md.a
make test                   # unit, fixture and CLI tests
sudo make install           # PREFIX=/usr/local; DESTDIR is honoured
```

With CMake (also for Visual Studio / MSVC):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

Installing puts the `html2md` binary, `libhtml2md.a`, `html2md.h`, a
pkg-config file and the `html2md(1)` man page in place.

## Usage

```
html2md [OPTION]... [FILE]...
```

html2md reads each `FILE` (or standard input when `FILE` is `-` or absent)
and writes Markdown to standard output.

### Common recipes

```sh
# Convert a file
html2md page.html > page.md

# Keep only the article of a downloaded page, with absolute links
curl -s https://example.com/post | html2md -m -b https://example.com/post

# Only the elements you care about, minus the noise
html2md --select 'article' --exclude '.share, .ad, .newsletter' page.html

# A GitHub repository page: just the README
html2md -s article -b https://github.com/owner/repo repo.html

# Convert a folder of pages into notes/NAME.md
html2md -d notes/ export/*.html

# A page whose sections are collapsed accordions (hidden panels)
html2md --keep-hidden transcript.html

# Plain text for search indexing or LLM ingestion
html2md -m --links=none --images=none --no-html page.html

# Documentation-style output: wrapped, reference links, front matter
html2md --wrap=80 --links=reference --front-matter page.html

# Strict CommonMark instead of GitHub Flavored Markdown
html2md --flavor=commonmark page.html
```

### Options

| Option | Description |
| --- | --- |
| `-o`, `--output=FILE` | Write to `FILE` instead of standard output. |
| `-d`, `--output-dir=DIR` | Write each input to `DIR/NAME.md` (the directory is created if needed). |
| `-e`, `--encoding=NAME` | Input encoding: `utf-8`, `windows-1252`, `iso-8859-1`, `iso-8859-15`, `us-ascii`, `utf-16le`, `utf-16be`. Default: BOM, then `<meta charset>`, then UTF-8 if valid, else Windows-1252. |
| `-b`, `--base-url=URL` | Resolve relative links and images against this absolute URL. `<base href>` is honoured too. |
| `-m`, `--main` | Keep only the main content (`<main>`, `role=main`, or the article) and drop navigation, headers, footers and sidebars. |
| `-s`, `--select=SELECTOR` | Convert only elements matching the CSS selector. |
| `-x`, `--exclude=SELECTOR` | Remove matching elements. Repeatable. |
| `--keep-hidden` | Keep elements hidden by `hidden`, `aria-hidden="true"` or `display:none`, such as collapsed accordion panels. |
| `--links=MODE` | `inline` (default), `reference`, or `none` (keep the text). |
| `--images=MODE` | `inline` (default), `alt` (alt text only), or `none`. |
| `--keep-data-uris` | Keep images embedded as `data:` URIs. |
| `--flavor=NAME` | `gfm` (default) or `commonmark`. |
| `--heading-style=S` | `atx` (default) or `setext`. |
| `--bullet=CHAR` | `-` (default), `*` or `+`. |
| `--emphasis=CHAR` | `*` (default) or `_`. |
| `--strong=CHAR` | `*` (default, `**`) or `_` (`__`). |
| `--code-style=S` | `fenced` (default) or `indented`. |
| `--fence=CHAR` | `` ` `` (default) or `~`. |
| `--br-style=S` | Hard line breaks: `backslash` (default) or `spaces`. |
| `-w`, `--wrap=COLUMNS` | Wrap paragraphs, list items and quotes; `0` (default) disables. |
| `--compact-tables` | Don't pad table cells to align columns. |
| `--no-html` | Never emit raw HTML (`<sup>`, `<kbd>`, `<br>` in cells, emphasis fallbacks). |
| `--preserve-nbsp` | Keep non-breaking spaces instead of plain spaces. |
| `--front-matter` | Prepend YAML front matter (title, description, author, date, lang, url). |
| `-q`, `--quiet` | Suppress warnings. |
| `-h`, `--help` / `-V`, `--version` | Show help or version. |

Long options can be abbreviated to any unambiguous prefix (`--front`), as
can option values (`--flavor=common`). Mistyped options get a suggestion:

```console
$ html2md --flavour=gfm page.html
html2md: unrecognized option '--flavour'
Did you mean '--flavor'?
Try 'html2md --help' for more information.
```

**Selectors** support type, `*`, `#id`, `.class`, `[attr]`, `[attr=value]`
(also `~=` `|=` `^=` `$=` `*=`, with an optional `i` flag), `:not()`,
`:is()`, `:where()`, `:first-child`, `:last-child`, `:only-child`, `:empty`,
the descendant, `>`, `+` and `~` combinators, and comma-separated lists.

**Exit status**: `0` success; `1` if an input could not be read or an output
could not be written (other inputs are still processed); `2` for invalid
usage.

See [`docs/html2md.1`](docs/html2md.1) (`man html2md` once installed).

## Library

```c
#include <html2md.h>

h2m_options opts;
h2m_options_init(&opts);            /* defaults; then customise */
opts.main_content = true;
opts.base_url = "https://example.com/";

char *md = NULL;
size_t len = 0;
if (h2m_convert(html, html_len, &opts, &md, &len) == H2M_OK) {
    fwrite(md, 1, len, stdout);
    h2m_free(md);
}
```

Link with `-lhtml2md` (or `pkg-config --cflags --libs html2md`). The library
is reentrant and keeps no global state. `h2m_convert` never fails on
malformed HTML. It returns an error only for invalid options, an unsupported
explicit encoding, or an invalid selector; a message is available through
`opts.error_buffer`. See [`include/html2md.h`](include/html2md.h) and
[`examples/library_usage.c`](examples/library_usage.c).

## How it works

```
bytes → decode (BOM, <meta charset>) → HTML5 tokenizer + tree builder → DOM
      → remove hidden/excluded/chrome → render into Markdown sinks → output
```

The rendering *sink* handles whitespace collapsing, block separation,
context-aware escaping, emphasis validation and wrapping. Containers (list
items, block quotes, links, table cells) render into child sinks that are
then indented, prefixed or flattened.
[`docs/DESIGN.md`](docs/DESIGN.md) covers the architecture and the research
behind it. [`docs/CONVERSION.md`](docs/CONVERSION.md) specifies the output
for every element.

## Testing

```sh
make test                          # unit (830+ checks), fixtures, CLI
make sanitize                      # the same under ASan + UBSan
make fuzz FUZZ_ITERATIONS=200000   # mutation fuzzing with output invariants
make roundtrip CMARK=/path/to/cmark-gfm   # CommonMark spec round trip
```

| Suite | Coverage |
| --- | --- |
| Unit tests | Buffers, UTF-8, entities, encodings, html5lib-style tree construction, selectors, RFC 3986 URL resolution, every conversion rule, CLI parsing |
| Fixtures | 22 realistic documents: blog, docs (Sphinx, highlight.js), Wikipedia, GitHub README, Google Docs export, email newsletter, malformed HTML, Windows-1252, CJK/RTL, math |
| CLI tests | Exit statuses, stdin/stdout, `-o`/`-d`, errors, overwrite protection |
| Round trip | All 652 CommonMark 0.31.2 examples in 15 option combinations. html2md's output rendered by cmark-gfm must convert back to the same Markdown and must be semantically equal to the input (except in the one combination that removes links and images on purpose) |
| Differential fuzzing | `tests/roundtrip_fuzz.py`: random mutations, same fixed-point check |
| Sanitizers and fuzzing | ASan/UBSan over every suite; a libFuzzer-compatible target |

## Project layout

```
include/html2md.h     public API
src/                  library (parser, converter, sink, ...) and CLI
docs/                 design, conversion reference, man page
tests/unit/           C unit tests
tests/fixtures/       golden HTML → Markdown documents (+ .args)
tests/fuzz/           fuzz target
tests/roundtrip*.py   CommonMark round-trip conformance and differential fuzzing
tools/                entity table generator
examples/             library usage example
```

## Limitations

- CSS stylesheets are not applied. Content hidden only by a stylesheet,
  such as collapsed menus, is kept; use `--exclude` for it. Conversely,
  panels hidden with the `hidden` attribute (collapsed accordions, inactive
  tab panels) are dropped; use `--keep-hidden` to convert them. If that
  leaves the output empty, html2md tells you so and names the option.
- Markdown has no equivalent for some HTML. `colspan`/`rowspan` are
  approximated with empty cells. Block content inside table cells is joined
  with `<br>`. Definition lists become bold terms followed by paragraphs.
- Other legacy encodings (Shift_JIS, GBK, KOI8-R, …) are decoded as UTF-8
  with a warning. Convert them to UTF-8 first (`iconv`).
- html2md does not sanitize HTML. Output may contain inline HTML such as
  `<sup>` unless `--no-html` is given, so sanitize before rendering
  untrusted content (see [SECURITY.md](SECURITY.md)).

## Contributing

Contributions are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT; see [LICENSE](LICENSE).
