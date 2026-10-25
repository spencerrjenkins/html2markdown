#!/bin/sh
# End-to-end tests of the html2md command-line tool: I/O handling, exit
# statuses and diagnostics.
#
# Usage: tests/run_cli_tests.sh [path/to/html2md]
#
# SPDX-License-Identifier: MIT

set -u

bin=${1:-build/html2md}
case $bin in
    /*) ;;
    *) bin=$(pwd)/$bin ;;
esac
if [ ! -x "$bin" ]; then
    echo "run_cli_tests: '$bin' is not an executable; build it first" >&2
    exit 2
fi

work=${TMPDIR:-/tmp}/h2m-cli.$$
mkdir -p "$work" || exit 2
trap 'rm -rf "$work"' EXIT INT TERM
cd "$work" || exit 2

pass=0
fail=0

ok() {
    pass=$((pass + 1))
}

bad() {
    echo "FAIL: $1"
    fail=$((fail + 1))
}

# expect_status NAME EXPECTED_STATUS COMMAND...
expect_status() {
    name=$1
    want=$2
    shift 2
    "$@" > out.txt 2> err.txt < /dev/null
    got=$?
    if [ "$got" -eq "$want" ]; then
        ok
    else
        bad "$name: exit status $got, expected $want"
        sed 's/^/    stderr: /' err.txt
    fi
}

# expect_file_eq NAME FILE EXPECTED_CONTENT
expect_file_eq() {
    printf '%s' "$3" > expected.txt
    if cmp -s "$2" expected.txt; then
        ok
    else
        bad "$1: unexpected content in $2"
        diff -u expected.txt "$2" | sed 's/^/    /' | head -20
    fi
}

expect_grep() {
    if grep -q -- "$3" "$2"; then
        ok
    else
        bad "$1: '$3' not found in $2"
        sed 's/^/    /' "$2" | head -10
    fi
}

printf '<h1>Title</h1><p>Hello <b>world</b></p>\n' > a.html
printf '<ul><li>one</li><li>two</li></ul>\n' > b.htm
printf '<p>Caf\351</p>\n' > latin1.html

# --- basic conversion -------------------------------------------------------
expect_status "file to stdout" 0 "$bin" a.html
expect_file_eq "file to stdout" out.txt '# Title

Hello **world**
'

"$bin" < a.html > out.txt 2> err.txt
expect_file_eq "stdin to stdout" out.txt '# Title

Hello **world**
'

"$bin" - < b.htm > out.txt
expect_file_eq "explicit - for stdin" out.txt '- one
- two
'

expect_status "multiple files" 0 "$bin" a.html b.htm
expect_file_eq "multiple files concatenated" out.txt '# Title

Hello **world**

- one
- two
'

expect_status "-o output file" 0 "$bin" -o result.md a.html
expect_file_eq "-o output file" result.md '# Title

Hello **world**
'
[ ! -s out.txt ] && ok || bad "-o: nothing on stdout"

expect_status "--output=FILE" 0 "$bin" --output=result2.md b.htm
expect_file_eq "--output=FILE" result2.md '- one
- two
'

expect_status "-d output dir" 0 "$bin" -d outdir a.html b.htm
expect_file_eq "-d a.md" outdir/a.md '# Title

Hello **world**
'
expect_file_eq "-d b.md" outdir/b.md '- one
- two
'

expect_status "empty input" 0 "$bin" /dev/null
[ ! -s out.txt ] && ok || bad "empty input produces no output"

# --- options ------------------------------------------------------------------
expect_status "encoding option" 0 "$bin" -e latin1 latin1.html
expect_file_eq "encoding option" out.txt 'Café
'
expect_status "auto-detected legacy encoding" 0 "$bin" latin1.html
expect_file_eq "auto-detected legacy encoding" out.txt 'Café
'
expect_status "select" 0 "$bin" -s h1 a.html
expect_file_eq "select" out.txt '# Title
'
expect_status "exclude" 0 "$bin" -x b a.html
expect_file_eq "exclude" out.txt '# Title

Hello
'
expect_status "combined short options" 0 "$bin" -mqw72 a.html

# --- help and version ---------------------------------------------------------
expect_status "--help" 0 "$bin" --help
expect_grep "--help text" out.txt "Usage: html2md"
expect_grep "--help lists --main" out.txt "--main"
expect_status "--version" 0 "$bin" --version
expect_grep "--version text" out.txt "html2md 1.0.0"

# --- usage errors (exit 2) --------------------------------------------------
expect_status "unknown option" 2 "$bin" --frobnicate a.html
expect_grep "unknown option message" err.txt "unrecognized option '--frobnicate'"
expect_grep "unknown option hint" err.txt "Try 'html2md --help'"
expect_status "typo suggestion" 2 "$bin" --flavour=gfm a.html
expect_grep "typo suggestion" err.txt "Did you mean '--flavor'?"
expect_status "bad enum value" 2 "$bin" --flavor=rst a.html
expect_status "bad wrap" 2 "$bin" --wrap=-1 a.html
expect_status "bad selector" 2 "$bin" --select='div[' a.html
expect_status "missing argument" 2 "$bin" a.html -o
expect_status "-o with -d" 2 "$bin" -o x.md -d dir a.html
expect_status "-d with stdin" 2 "$bin" -d dir -
expect_status "bad encoding" 2 "$bin" --encoding=koi8-r a.html

# --- runtime errors (exit 1) ------------------------------------------------
expect_status "missing file" 1 "$bin" does-not-exist.html
expect_grep "missing file message" err.txt "cannot open 'does-not-exist.html'"
expect_status "missing file does not stop others" 1 "$bin" does-not-exist.html a.html
expect_grep "other files still converted" out.txt "# Title"
expect_status "directory as input" 1 "$bin" outdir
expect_grep "directory message" err.txt "Is a directory"
cp a.html same.html
expect_status "refuse to overwrite input" 1 "$bin" -o same.html same.html
expect_file_eq "input left intact" same.html '<h1>Title</h1><p>Hello <b>world</b></p>
'
expect_status "unwritable output" 1 "$bin" -o /nonexistent-dir/x.md a.html
expect_status "output dir is a file" 1 "$bin" -d a.html b.htm

# --- special input ---------------------------------------------------------------
printf '<p>nul\000byte</p>' > nul.html
expect_status "NUL byte input" 0 "$bin" nul.html
expect_file_eq "NUL byte input" out.txt 'nulbyte
'
printf '\357\273\277<p>bom</p>' > bom.html
expect_status "UTF-8 BOM" 0 "$bin" bom.html
expect_file_eq "UTF-8 BOM" out.txt 'bom
'
# --- empty output is explained, not silent ----------------------------------
{
    printf '<div hidden><p>'
    i=0
    while [ $i -lt 60 ]; do printf 'Hidden panel text that the reader wants. '; i=$((i + 1)); done
    printf '</p></div>'
} > hidden.html
expect_status "hidden-only document" 0 "$bin" hidden.html
[ ! -s out.txt ] && ok || bad "hidden-only document: expected empty output"
expect_grep "empty output is explained" err.txt "the output is empty"
expect_grep "empty output suggests a flag" err.txt "keep-hidden"
expect_status "--keep-hidden recovers it" 0 "$bin" --keep-hidden hidden.html
[ -s out.txt ] && ok || bad "--keep-hidden: expected content"
[ ! -s err.txt ] && ok || bad "--keep-hidden: expected no warning"
expect_status "--quiet silences the diagnosis" 0 "$bin" -q hidden.html
[ ! -s err.txt ] && ok || bad "--quiet: stderr should be empty"

printf '<meta charset="shift_jis"><p>x</p>' > sjis.html
expect_status "unsupported meta charset warns" 0 "$bin" sjis.html
expect_grep "unsupported meta charset warning" err.txt "warning"
expect_status "--quiet suppresses warnings" 0 "$bin" -q sjis.html
[ ! -s err.txt ] && ok || bad "--quiet: stderr should be empty"

echo "cli: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
