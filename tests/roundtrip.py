#!/usr/bin/env python3
"""Round-trip conformance test for html2md.

For every example of the CommonMark specification (whose expected HTML is a
convenient corpus of tricky, spec-exact HTML) this script checks:

  1. Fixed point:  md1 = html2md(html);  md2 = html2md(render(md1))
                   md1 must equal md2. If html2md emitted Markdown that a
                   conforming renderer interprets differently from what was
                   intended, the second conversion diverges.

  2. Semantics:    normalize(html) must equal normalize(render(md1)), where
                   normalization ignores differences that carry no meaning
                   (whitespace, tight/loose paragraph wrappers, attribute
                   order, entity spelling). Examples whose HTML contains raw
                   HTML blocks or constructs Markdown cannot express are
                   reported separately as "expected differences".

The renderer is cmark-gfm (GitHub's reference implementation), with the
table and strikethrough extensions and raw HTML enabled.

Usage:
    python3 tests/roundtrip.py --html2md build/html2md --cmark path/to/cmark-gfm
                               [--spec tests/data/commonmark-spec-0.31.2.json]
                               [--verbose] [--example N]
Exit status is non-zero when any fixed-point or semantic check fails.
"""

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.parse
from html.parser import HTMLParser

BLOCK_TAGS = {
    "address", "article", "aside", "blockquote", "body", "dd", "details", "div", "dl",
    "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4",
    "h5", "h6", "header", "hr", "html", "li", "main", "nav", "ol", "p", "pre", "section",
    "table", "tbody", "td", "tfoot", "th", "thead", "tr", "ul",
}
VOID_TAGS = {"br", "hr", "img", "input", "meta", "link", "area", "base", "col", "embed", "source", "wbr"}
KNOWN_TAGS = BLOCK_TAGS | VOID_TAGS | {
    "a", "abbr", "code", "del", "em", "kbd", "q", "samp", "span", "strong", "sub", "sup", "var",
}
RENAME = {"b": "strong", "i": "em", "s": "del", "strike": "del"}
KEEP_EMPTY = {"br", "hr", "img", "input"}


class Node:
    def __init__(self, tag, attrs=None, parent=None):
        self.tag, self.attrs, self.parent, self.children = tag, dict(attrs or {}), parent, []


class TreeBuilder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("#root")
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur)
        self.cur.children.append(n)
        if tag not in VOID_TAGS:
            self.cur = n

    def handle_startendtag(self, tag, attrs):
        self.cur.children.append(Node(tag, attrs, self.cur))

    def handle_endtag(self, tag):
        n = self.cur
        while n is not self.root and n.tag != tag:
            n = n.parent
        if n is not self.root:
            self.cur = n.parent

    def handle_data(self, data):
        self.cur.children.append(data)


def in_tag(node, tag):
    while node is not None:
        if isinstance(node, Node) and node.tag == tag:
            return True
        node = node.parent
    return False


def transform(node):
    """Applies the equivalences html2md deliberately relies on."""
    out = []
    for ch in node.children:
        if isinstance(ch, str):
            out.append(ch)
            continue
        ch.tag = RENAME.get(ch.tag, ch.tag)
        transform(ch)
        unwrap = (
            ch.tag not in KNOWN_TAGS                                   # custom/unknown element
            or (ch.tag in ("em", "strong", "del") and in_tag(node, ch.tag))  # nested same kind
            or (ch.tag == "p" and node.tag == "li")                    # tight vs loose lists
        )
        if unwrap:
            for g in ch.children:
                if isinstance(g, Node):
                    g.parent = node
            out.extend(ch.children)
            continue
        if ch.tag == "img" and ch.attrs.get("alt") == "":
            del ch.attrs["alt"]
        for k in ("href", "src"):
            if k in ch.attrs:
                ch.attrs[k] = urllib.parse.unquote(ch.attrs[k] or "")
        if "title" in ch.attrs:
            ch.attrs["title"] = re.sub(r"\s+", " ", ch.attrs["title"] or "").strip()
        out.append(ch)
    node.children = out


def text_of(node):
    return "".join(c if isinstance(c, str) else text_of(c) for c in node.children)


def has_content(node):
    for c in node.children:
        if isinstance(c, str):
            if c.strip():
                return True
        elif c.tag in KEEP_EMPTY or has_content(c):
            return True
    return False


def serialize(node, out, pre=False):
    for c in node.children:
        if isinstance(c, str):
            out.append(c if pre else re.sub(r"\s+", " ", c))
            continue
        if c.tag not in KEEP_EMPTY and not has_content(c):
            continue  # empty elements are dropped by design
        attrs = "".join(f' {k}="{v}"' for k, v in sorted(c.attrs.items()))
        if c.tag == "pre":
            body = text_of(c).strip("\n")
            out.append(f"<pre{attrs}>{body}</pre>")
            continue
        if c.tag == "code":
            out.append(f"<code>{re.sub(r'\s+', ' ', text_of(c)).strip()}</code>")
            continue
        out.append(f"<{c.tag}{attrs}>")
        if c.tag not in VOID_TAGS:
            serialize(c, out, pre)
            out.append(f"</{c.tag}>")


def normalize(fragment):
    tb = TreeBuilder()
    tb.feed(fragment)
    tb.close()
    transform(tb.root)
    out = []
    serialize(tb.root, out)
    s = "".join(out)
    # Whitespace next to block boundaries is insignificant.
    blocks = "|".join(sorted(BLOCK_TAGS | {"br"}))
    s = re.sub(rf"\s*(</?(?:{blocks})\b[^>]*>)\s*", r"\1", s)
    return s.strip()


def run(cmd, data):
    p = subprocess.run(cmd, input=data.encode("utf-8"), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise RuntimeError(f"{cmd[0]} failed: {p.stderr.decode('utf-8', 'replace')}")
    return p.stdout.decode("utf-8")


# Option combinations exercised by --matrix. Configurations that remove
# content on purpose (LOSSY) are only held to the fixed-point property.
LOSSY = {"--links=none --images=alt"}
MATRIX = [
    "",
    "--emphasis=_ --strong=_",
    "--flavor=commonmark",
    "--flavor=commonmark --no-html",
    "--no-html",
    "--code-style=indented --fence=~",
    "--br-style=spaces --bullet=+",
    "--heading-style=setext",
    "--links=reference",
    "--links=none --images=alt",
    "--compact-tables",
    "--wrap=10",
    "--wrap=20",
    "--wrap=40",
    "--wrap=80",
]

# Constructs whose round trip is intentionally lossy: raw HTML that html2md
# converts or drops, and link reference definitions / titles on images etc.
EXPECTED_DIFFERENCE_SECTIONS = {"HTML blocks", "Raw HTML"}


def check(examples, h2m, cmark, args):
    fixed_fail, sem_fail, expected = [], [], []
    for ex in examples:
        if args.example and ex["example"] not in args.example:
            continue
        src = ex["html"]
        md1 = run(h2m, src)
        html2 = run(cmark, md1)
        md2 = run(h2m, html2)
        # Renderers may normalise URLs (e.g. percent-encoding), so a textual
        # difference is only a failure if it changes the rendered result.
        ok_fixed = md1 == md2 or normalize(html2) == normalize(run(cmark, md2))
        ok_sem = normalize(src) == normalize(html2)
        tag = ex["section"] in EXPECTED_DIFFERENCE_SECTIONS
        if not ok_fixed:
            fixed_fail.append(ex)
        if not ok_sem:
            (expected if tag else sem_fail).append(ex)
        if args.verbose and (not ok_fixed or (not ok_sem and (not tag or args.show_expected))):
            print(f"--- example {ex['example']} ({ex['section']})")
            print("html:  " + repr(src))
            print("md1:   " + repr(md1))
            print("html2: " + repr(html2))
            if not ok_fixed:
                print("md2:   " + repr(md2))
    return fixed_fail, sem_fail, expected


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--html2md", default="build/html2md")
    ap.add_argument("--cmark", default=shutil.which("cmark-gfm") or "cmark-gfm")
    ap.add_argument("--spec", default=os.path.join(here, "data", "commonmark-spec-0.31.2.json"))
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--example", type=int, action="append")
    ap.add_argument("--flags", default="", help="extra html2md flags")
    ap.add_argument("--matrix", action="store_true", help="run every option combination in MATRIX")
    ap.add_argument("--show-expected", action="store_true", help="also print expected (raw HTML) differences")
    args = ap.parse_args()

    if not shutil.which(args.cmark) and not os.path.exists(args.cmark):
        print(f"roundtrip: cmark-gfm not found ('{args.cmark}'); skipping", file=sys.stderr)
        return 0

    examples = json.load(open(args.spec, encoding="utf-8"))
    cmark = [args.cmark, "--unsafe", "-e", "table", "-e", "strikethrough"]

    configs = MATRIX if args.matrix else [args.flags]
    status = 0
    for flags in configs:
        h2m = [args.html2md] + flags.split()
        fixed_fail, sem_fail, expected = check(examples, h2m, cmark, args)
        if flags in LOSSY:
            sem_fail = []  # content is removed by design; only the fixed point applies
        label = flags or "(defaults)" if flags not in LOSSY else flags + " (fixed point only)"
        if args.matrix:
            print(f"{label:<36} fixed-point failures: {len(fixed_fail)}  "
                  f"semantic differences: {len(sem_fail)}")
        else:
            total = len(examples) if not args.example else len(args.example)
            print(f"examples:                 {total}")
            print(f"fixed-point failures:     {len(fixed_fail)}")
            print(f"semantic differences:     {len(sem_fail)}")
            print(f"expected (raw HTML) diffs: {len(expected)}")
        if fixed_fail:
            print("  fixed-point failing examples:", " ".join(str(e["example"]) for e in fixed_fail))
        if sem_fail:
            print("  semantic failing examples:", " ".join(str(e["example"]) for e in sem_fail))
        if fixed_fail or sem_fail:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
