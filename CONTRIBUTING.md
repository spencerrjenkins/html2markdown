# Contributing

Thanks for helping improve html2md. This file covers the build, the tests
and the conventions the code follows.

## Getting started

```sh
make            # build build/html2md and build/libhtml2md.a
make test       # unit tests, golden fixtures, CLI tests
make debug      # unoptimised build with debug symbols in build-debug/
```

Everything needs only a C compiler and `make`. Python 3 is used for the
optional round-trip tests and the entity table generator; cmark-gfm is used
as the reference renderer for round-trip testing.

## Running the full test suite

```sh
make test                                   # fast suites (a few seconds)
make sanitize                               # all suites under ASan + UBSan
make fuzz FUZZ_ITERATIONS=200000            # mutation fuzzing
make roundtrip CMARK=/path/to/cmark-gfm     # CommonMark conformance matrix
python3 tests/roundtrip_fuzz.py --cmark /path/to/cmark-gfm tests/fixtures/*.html
```

Please run at least `make test` and `make sanitize` before opening a pull
request. If you touch the parser, the sink or escaping, run the round-trip
suites too: they are what keeps the output provably correct.

## Where things live

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture and
[docs/CONVERSION.md](docs/CONVERSION.md) for the output reference. In short:

| Change | Files |
| --- | --- |
| How an element converts | `src/convert.c`, plus the tag's flags in `src/tags.h` |
| Escaping, whitespace, wrapping, emphasis | `src/sink.c` |
| HTML parsing and error recovery | `src/parser.c` |
| A new command-line option | `src/cli.c`, `include/html2md.h`, `src/html2md.c`, `docs/html2md.1`, `README.md` |
| Selector syntax | `src/selector.c` |

## Adding tests

- **A conversion rule** belongs in `tests/unit/test_convert.c` as a
  `CHECK_MD("<html>", "expected markdown\n")` line. Keep cases minimal and
  focused; they double as the specification.
- **A parsing behaviour** belongs in `tests/unit/test_parser.c`, written as
  an html5lib-style tree dump so it can be compared with browser behaviour.
- **A whole document** belongs in `tests/fixtures/`: add `name.html` (plus
  `name.args` for options), run `sh tests/run_fixtures.sh --update`, and
  **read the generated `name.md`** before committing it.
- **CLI behaviour** belongs in `tests/run_cli_tests.sh` (end to end) or
  `tests/unit/test_cli.c` (argument parsing).

A bug fix should come with the smallest input that reproduces it.

## Conventions

- **Language:** C11 that also compiles as C99 and C17, standard library
  only, no compiler extensions beyond `__attribute__` guarded by
  `#if defined(__GNUC__) || defined(__clang__)`.
- **Warnings:** the build must stay silent with the Makefile's warning set;
  CI builds with `-Werror`.
- **Style:** 4-space indent, no tabs, lines up to 120 columns, braces on the
  next line for functions and on the same line for control statements,
  lowercase `snake_case`, `h2m_` prefix on every non-static symbol.
  Comments explain *why*; the code should show *what*.
- **Memory:** DOM nodes and strings live in the document arena; everything
  else is `h2m_buf`/`malloc` and must be freed on every path (the tests run
  under ASan, and macOS `leaks` is used for leak checks).
- **Robustness:** any new loop over untrusted input needs a bound. New
  recursion needs a depth limit. See "Resource limits" in the design
  document.
- **Portability:** no POSIX-only functions outside `src/main.c`, which has
  the `_WIN32` fallbacks.

## Regenerating the entity table

`src/entities_data.h` is generated and checked in:

```sh
curl -o tools/entities.json https://html.spec.whatwg.org/entities.json
make entities
```

## Reporting bugs

Please include the smallest HTML that reproduces the problem, the exact
command line, the output you got and the output you expected. A page URL
alone is hard to act on, because pages change.
