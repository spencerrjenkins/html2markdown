/*
 * Minimal unit test framework.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_TESTLIB_H
#define H2M_TESTLIB_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern int tl_checks;
extern int tl_failures;
extern const char *tl_current;

void tl_fail(const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* Prints a readable diff of two strings (with escapes for control chars). */
void tl_fail_str(const char *file, int line, const char *what, const char *expected, const char *actual);

#define CHECK(cond)                                                    \
    do {                                                               \
        tl_checks++;                                                   \
        if (!(cond))                                                   \
            tl_fail(__FILE__, __LINE__, "check failed: %s", #cond);    \
    } while (0)

#define CHECK_EQ_INT(expected, actual)                                                   \
    do {                                                                                 \
        long long e_ = (long long)(expected), a_ = (long long)(actual);                  \
        tl_checks++;                                                                     \
        if (e_ != a_)                                                                    \
            tl_fail(__FILE__, __LINE__, "%s: expected %lld, got %lld", #actual, e_, a_); \
    } while (0)

#define CHECK_EQ_STR(expected, actual)                                         \
    do {                                                                       \
        const char *e_ = (expected), *a_ = (actual);                           \
        tl_checks++;                                                           \
        if (!e_ || !a_ || strcmp(e_, a_) != 0)                                 \
            tl_fail_str(__FILE__, __LINE__, #actual, e_ ? e_ : "(null)",       \
                        a_ ? a_ : "(null)");                                   \
    } while (0)

typedef void (*tl_suite_fn)(void);

/* Suites (one per test file). */
void suite_utils(void);
void suite_parser(void);
void suite_selector(void);
void suite_url(void);
void suite_convert(void);
void suite_cli(void);

#endif /* H2M_TESTLIB_H */
