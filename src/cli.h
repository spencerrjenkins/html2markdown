/*
 * Command-line argument parsing for the html2md tool.
 *
 * Supports GNU-style conventions: long options with "=value" or a separate
 * argument, unambiguous abbreviations of long options, clustered short
 * options (-mq, -ofile), "--" to end option processing and "-" for stdin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_CLI_H
#define H2M_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "html2md.h"
#include "buf.h"

typedef struct cli_config {
    h2m_options opts;
    const char *output;     /* -o FILE, NULL = stdout */
    const char *output_dir; /* -d DIR, NULL = not used */
    const char **inputs;    /* positional arguments ("-" = stdin) */
    size_t ninputs;
    bool quiet;
    h2m_buf exclude;        /* accumulated --exclude selectors */
} cli_config;

typedef enum cli_result {
    CLI_RUN,        /* proceed with conversion */
    CLI_DONE,       /* --help / --version printed; exit successfully */
    CLI_USAGE_ERROR /* message printed; exit with status 2 */
} cli_result;

/* Exit statuses. */
#define H2M_EXIT_OK 0
#define H2M_EXIT_FAILURE 1
#define H2M_EXIT_USAGE 2

/*
 * Parses argv into cfg (which is initialised by this call). Help and
 * version text go to `out`, diagnostics to `err`. Call cli_config_free
 * afterwards regardless of the result.
 */
cli_result cli_parse(int argc, char **argv, cli_config *cfg, FILE *out, FILE *err);

void cli_config_free(cli_config *cfg);

void cli_print_help(FILE *out);
void cli_print_version(FILE *out);

/* Short usage hint printed after usage errors. */
void cli_print_try_help(FILE *err);

extern const char *cli_program_name;

#endif /* H2M_CLI_H */
