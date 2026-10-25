#!/bin/sh
# Golden-file tests: converts tests/fixtures/NAME.html (with the options in
# NAME.args, if present) and compares the result with NAME.md.
#
# Usage: tests/run_fixtures.sh [--update] [path/to/html2md]
#   --update   rewrite the expected .md files from the current output
#              (review the resulting diff before committing!)
#
# SPDX-License-Identifier: MIT

set -u

update=0
if [ "${1:-}" = "--update" ]; then
    update=1
    shift
fi
bin=${1:-build/html2md}
dir=$(cd "$(dirname "$0")/fixtures" && pwd)

if [ ! -x "$bin" ]; then
    echo "run_fixtures: '$bin' is not an executable; build it first" >&2
    exit 2
fi

tmp=${TMPDIR:-/tmp}/h2m-fixtures.$$
mkdir -p "$tmp" || exit 2
trap 'rm -rf "$tmp"' EXIT INT TERM

pass=0
fail=0
for html in "$dir"/*.html; do
    name=$(basename "$html" .html)
    expected="$dir/$name.md"
    args=""
    if [ -f "$dir/$name.args" ]; then
        args=$(cat "$dir/$name.args")
    fi
    # $args is intentionally unquoted so that it splits into options.
    # shellcheck disable=SC2086
    if ! "$bin" $args "$html" > "$tmp/$name.md" 2> "$tmp/$name.err"; then
        echo "FAIL $name: html2md exited with an error"
        cat "$tmp/$name.err"
        fail=$((fail + 1))
        continue
    fi
    if [ "$update" -eq 1 ]; then
        cp "$tmp/$name.md" "$expected"
        echo "updated $name.md"
        continue
    fi
    if [ ! -f "$expected" ]; then
        echo "FAIL $name: missing expected output $name.md (run with --update)"
        fail=$((fail + 1))
        continue
    fi
    if cmp -s "$expected" "$tmp/$name.md"; then
        pass=$((pass + 1))
    else
        echo "FAIL $name"
        diff -u "$expected" "$tmp/$name.md" | head -40
        fail=$((fail + 1))
    fi
done

if [ "$update" -eq 1 ]; then
    exit 0
fi
echo "fixtures: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
