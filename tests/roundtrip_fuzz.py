#!/usr/bin/env python3
"""Differential round-trip fuzzer.

Mutates seed HTML documents and, for each mutant, checks the fixed-point
property  md1 == html2md(render(md1))  where md1 = html2md(mutant) and
render is cmark-gfm. A violation means html2md produced Markdown whose
rendering differs from what html2md intended - typically an escaping bug.
When the Markdown text differs only by renderer normalisation, the rendered
HTML of both versions is compared instead.

Usage:
    python3 tests/roundtrip_fuzz.py --cmark path/to/cmark-gfm [--iterations N]
                                    [--seed S] [--flags "..."] SEED_FILE...
Failing inputs are written to --out (default: fuzz-failures/).
"""

import argparse
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from roundtrip import normalize  # noqa: E402

TOKENS = [
    "<p>", "</p>", "<div>", "</div>", "<b>", "</b>", "<i>", "</i>", "<em>", "</em>", "<strong>",
    "</strong>", "<a href=\"/x\">", "</a>", "<code>", "</code>", "<pre>", "</pre>", "<ul>", "<ol>",
    "<li>", "</li>", "</ul>", "<blockquote>", "</blockquote>", "<h1>", "<h2>", "</h2>", "<br>",
    "<hr>", "<table><tr><td>", "</td><td>", "</table>", "<th>", "<del>", "</del>", "<img src=i alt=a>",
    "<sup>", "<kbd>", "<dl><dt>", "<dd>", " ", "\n", "*", "_", "`", "~", "#", "-", "+", "=", ">",
    "<", "&lt;", "&amp;", "&gt;", "[", "]", "(", ")", "!", "\\", "|", ":", "1.", "2)", "&nbsp;",
    "***", "___", "```", "~~~", "---", "http://example.com", "a", "b", "word", "é", "中",
]


def mutate(rng, text):
    for _ in range(rng.randint(1, 6)):
        pos = rng.randint(0, len(text))
        op = rng.random()
        if op < 0.55:
            text = text[:pos] + rng.choice(TOKENS) + text[pos:]
        elif op < 0.8 and text:
            end = min(len(text), pos + rng.randint(1, 12))
            text = text[:pos] + text[end:]
        elif text:
            start = rng.randint(0, len(text) - 1)
            chunk = text[start:start + rng.randint(1, 24)]
            text = text[:pos] + chunk + text[pos:]
    return text[:4000]


def run(cmd, data):
    p = subprocess.run(cmd, input=data.encode("utf-8"), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=30)
    if p.returncode != 0:
        raise RuntimeError(f"{cmd[0]} exited with {p.returncode}: {p.stderr[:200]!r}")
    return p.stdout.decode("utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--html2md", default="build/html2md")
    ap.add_argument("--cmark", required=True)
    ap.add_argument("--iterations", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--flags", default="")
    ap.add_argument("--out", default="fuzz-failures")
    ap.add_argument("seeds", nargs="+")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    corpus = []
    for path in args.seeds:
        with open(path, encoding="utf-8", errors="replace") as fh:
            corpus.append(fh.read())
    h2m = [args.html2md] + args.flags.split()
    cmark = [args.cmark, "--unsafe", "-e", "table", "-e", "strikethrough"]

    failures = 0
    for i in range(args.iterations):
        src = mutate(rng, rng.choice(corpus))
        if "<math" in src or "math/tex" in src:
            continue  # TeX output is not CommonMark; the renderer cannot judge it
        md1 = run(h2m, src)
        html2 = run(cmark, md1)
        md2 = run(h2m, html2)
        if md1 == md2:
            continue
        if normalize(html2) == normalize(run(cmark, md2)):
            continue
        failures += 1
        os.makedirs(args.out, exist_ok=True)
        path = os.path.join(args.out, f"case{failures:03d}.html")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(src)
        print(f"--- failure {failures} (saved to {path})")
        print("md1:   " + repr(md1[:400]))
        print("md2:   " + repr(md2[:400]))
        if failures >= 25:
            break
    print(f"roundtrip_fuzz: {args.iterations} iterations, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
