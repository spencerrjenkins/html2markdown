/*
 * Unit test runner.
 *
 * Usage: unit_tests [SUITE...]   (default: all suites)
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdlib.h>

#include "testlib.h"

int tl_checks = 0;
int tl_failures = 0;
const char *tl_current = "";

void tl_fail(const char *file, int line, const char *fmt, ...)
{
    tl_failures++;
    fprintf(stderr, "FAIL [%s] %s:%d: ", tl_current, file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void print_escaped(const char *s)
{
    fputc('"', stderr);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n')
            fputs("\\n\"\n       \"", stderr);
        else if (c == '\t')
            fputs("\\t", stderr);
        else if (c == '"')
            fputs("\\\"", stderr);
        else if (c < 0x20)
            fprintf(stderr, "\\x%02x", c);
        else
            fputc(c, stderr);
    }
    fputs("\"\n", stderr);
}

void tl_fail_str(const char *file, int line, const char *what, const char *expected, const char *actual)
{
    tl_failures++;
    fprintf(stderr, "FAIL [%s] %s:%d: %s\n  expected: ", tl_current, file, line, what);
    print_escaped(expected);
    fputs("  actual:   ", stderr);
    print_escaped(actual);
}

static const struct {
    const char *name;
    tl_suite_fn fn;
} suites[] = {
    {"utils", suite_utils},       {"parser", suite_parser}, {"selector", suite_selector},
    {"url", suite_url},           {"convert", suite_convert}, {"cli", suite_cli},
};

int main(int argc, char **argv)
{
    size_t n = sizeof suites / sizeof suites[0];
    for (size_t i = 0; i < n; i++) {
        bool run = argc < 2;
        for (int a = 1; a < argc; a++) {
            if (strcmp(argv[a], suites[i].name) == 0)
                run = true;
        }
        if (!run)
            continue;
        int before_checks = tl_checks, before_fail = tl_failures;
        tl_current = suites[i].name;
        suites[i].fn();
        printf("%-10s %5d checks, %d failed\n", suites[i].name, tl_checks - before_checks,
               tl_failures - before_fail);
    }
    printf("TOTAL      %5d checks, %d failed\n", tl_checks, tl_failures);
    return tl_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
