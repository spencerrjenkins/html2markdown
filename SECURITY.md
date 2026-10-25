# Security

## Threat model

html2md is designed to be run on **untrusted HTML**: that is its normal
input. The library never executes scripts, never loads external resources
(no network access, no file access from the library) and never follows
links; it only reads the bytes it is given.

Parsing and conversion are bounded: the nesting depth, the number of
attributes and active formatting elements, table geometry and recursion
depth all have documented limits (see "Resource limits" in
[docs/DESIGN.md](docs/DESIGN.md)), so hostile input cannot make html2md hang
or allocate without bound. Memory usage stays proportional to the input.

Every release is tested under AddressSanitizer and UndefinedBehaviorSanitizer
across the whole test suite, and with mutation fuzzing that checks output
invariants (valid UTF-8, no NUL bytes, deterministic output).

## What html2md does *not* do

- **It is not a sanitizer.** By default the output may contain a small,
  fixed set of inline HTML tags (`<sup>`, `<sub>`, `<kbd>`, `<br>`,
  `<em>`/`<strong>`/`<del>` and `<code>` as emphasis fallbacks, and HTML
  tables in CommonMark mode). Attribute values (URLs, titles, alt text) come
  from the input. If you render the resulting Markdown as HTML for other
  users, run the result through an HTML sanitizer, or convert with
  `--no-html` and a Markdown renderer that escapes raw HTML.
- **It does not validate URLs.** `javascript:`, `vbscript:` and `data:`
  URLs are dropped from links, and `data:` image sources are dropped unless
  `--keep-data-uris` is given, but any other scheme is preserved as written.
  Treat links in the output as untrusted.
- **It does not protect against decompression bombs**: give it bounded
  input if that matters to your deployment.

## Memory allocation policy

Small internal allocation failures abort the process with a message, the
same policy as cmark. The single input-sized allocation is checked and
reported as `H2M_ERR_NOMEM`. If you embed the library in a process where
aborting is unacceptable, constrain the input size.

## Reporting a vulnerability

Please report crashes, hangs, out-of-bounds accesses or memory exhaustion
privately to the maintainers rather than in a public issue, and include the
input that triggers the problem (a file attachment or base64 blob is ideal)
together with the command line or option struct you used.
