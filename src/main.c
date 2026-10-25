/*
 * html2md command-line tool: file handling around the library.
 *
 * SPDX-License-Identifier: MIT
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#define h2m_mkdir(path) _mkdir(path)
#else
#include <unistd.h>
#define h2m_mkdir(path) mkdir((path), 0777)
#endif

#include "buf.h"
#include "cli.h"
#include "html2md.h"

typedef struct {
    const char *name;
} warn_ctx;

static void warn_cb(void *user, const char *msg)
{
    const warn_ctx *w = user;
    fprintf(stderr, "%s: warning: %s: %s\n", cli_program_name, w->name, msg);
}

/* Reads a whole stream. Returns false on a read error. */
static bool read_stream(FILE *fp, h2m_buf *out)
{
    char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, fp)) > 0)
        h2m_buf_append(out, chunk, n);
    if (!out->data)
        h2m_buf_append(out, "", 0);
    return !ferror(fp);
}

static bool read_input(const char *path, h2m_buf *out)
{
    if (strcmp(path, "-") == 0) {
        if (!read_stream(stdin, out)) {
            fprintf(stderr, "%s: error reading standard input: %s\n", cli_program_name, strerror(errno));
            return false;
        }
        return true;
    }
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s: %s: Is a directory\n", cli_program_name, path);
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "%s: cannot open '%s': %s\n", cli_program_name, path, strerror(errno));
        return false;
    }
    bool ok = read_stream(fp, out);
    if (!ok)
        fprintf(stderr, "%s: error reading '%s': %s\n", cli_program_name, path, strerror(errno));
    fclose(fp);
    return ok;
}

/* DIR/NAME.md for an input path. */
static void output_path_for(const char *dir, const char *input, h2m_buf *out)
{
    const char *base = input;
    for (const char *p = input; *p; p++) {
        if (*p == '/'
#ifdef _WIN32
            || *p == '\\'
#endif
        )
            base = p + 1;
    }
    size_t len = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot && dot != base)
        len = (size_t)(dot - base);
    if (len == 0) {
        base = "index";
        len = 5;
    }
    h2m_buf_puts(out, dir);
    size_t dl = strlen(dir);
    if (dl && dir[dl - 1] != '/'
#ifdef _WIN32
        && dir[dl - 1] != '\\'
#endif
    )
        h2m_buf_putc(out, '/');
    h2m_buf_append(out, base, len);
    h2m_buf_puts(out, ".md");
}

static bool ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return true;
        fprintf(stderr, "%s: '%s' exists and is not a directory\n", cli_program_name, dir);
        return false;
    }
    if (h2m_mkdir(dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "%s: cannot create directory '%s': %s\n", cli_program_name, dir, strerror(errno));
        return false;
    }
    return true;
}

/* True when two paths name the same existing file. */
static bool same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return false;
#ifdef _WIN32
    return strcmp(a, b) == 0;
#else
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

static bool write_all(FILE *fp, const char *data, size_t len, const char *name)
{
    if (len && fwrite(data, 1, len, fp) != len) {
        fprintf(stderr, "%s: error writing '%s': %s\n", cli_program_name, name, strerror(errno));
        return false;
    }
    return true;
}

static bool close_output(FILE *fp, const char *name)
{
    if (fflush(fp) != 0 || ferror(fp)) {
        fprintf(stderr, "%s: error writing '%s': %s\n", cli_program_name, name, strerror(errno));
        if (fp != stdout)
            fclose(fp);
        return false;
    }
    if (fp != stdout && fclose(fp) != 0) {
        fprintf(stderr, "%s: error closing '%s': %s\n", cli_program_name, name, strerror(errno));
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    cli_config cfg;
    cli_result r = cli_parse(argc, argv, &cfg, stdout, stderr);
    if (r != CLI_RUN) {
        cli_config_free(&cfg);
        if (r == CLI_DONE)
            return fflush(stdout) == 0 ? H2M_EXIT_OK : H2M_EXIT_FAILURE;
        return H2M_EXIT_USAGE;
    }

    static const char *stdin_only[] = {"-"};
    const char **inputs = cfg.ninputs ? cfg.inputs : stdin_only;
    size_t ninputs = cfg.ninputs ? cfg.ninputs : 1;

    if (!cfg.ninputs && isatty(fileno(stdin))) {
        fprintf(stderr, "%s: no input files and standard input is a terminal\n", cli_program_name);
        fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", cli_program_name);
        cli_print_try_help(stderr);
        cli_config_free(&cfg);
        return H2M_EXIT_USAGE;
    }

    if (cfg.output) {
        for (size_t i = 0; i < ninputs; i++) {
            if (strcmp(inputs[i], "-") != 0 && same_file(inputs[i], cfg.output)) {
                fprintf(stderr, "%s: refusing to overwrite input file '%s'\n", cli_program_name, cfg.output);
                cli_config_free(&cfg);
                return H2M_EXIT_FAILURE;
            }
        }
    }
    if (cfg.output_dir && !ensure_dir(cfg.output_dir)) {
        cli_config_free(&cfg);
        return H2M_EXIT_FAILURE;
    }

    int status = H2M_EXIT_OK;
    FILE *shared = NULL; /* stdout or the -o file */
    const char *shared_name = cfg.output ? cfg.output : "standard output";
    bool wrote_any = false;
    char errbuf[256];

    for (size_t i = 0; i < ninputs; i++) {
        const char *in_name = inputs[i];
        const char *display = strcmp(in_name, "-") == 0 ? "standard input" : in_name;
        h2m_buf html = H2M_BUF_INIT;
        if (!read_input(in_name, &html)) {
            h2m_buf_free(&html);
            status = H2M_EXIT_FAILURE;
            continue;
        }

        h2m_options opts = cfg.opts;
        warn_ctx wctx = {display};
        opts.warning = cfg.quiet ? NULL : warn_cb;
        opts.warning_data = &wctx;
        opts.error_buffer = errbuf;
        opts.error_buffer_size = sizeof errbuf;
        char *md = NULL;
        size_t md_len = 0;
        h2m_status st = h2m_convert(html.data, html.len, &opts, &md, &md_len);
        h2m_buf_free(&html);
        if (st != H2M_OK) {
            fprintf(stderr, "%s: %s: %s\n", cli_program_name, display, errbuf[0] ? errbuf : h2m_status_string(st));
            status = H2M_EXIT_FAILURE;
            continue;
        }

        if (cfg.output_dir) {
            h2m_buf path = H2M_BUF_INIT;
            output_path_for(cfg.output_dir, in_name, &path);
            if (same_file(in_name, path.data)) {
                fprintf(stderr, "%s: refusing to overwrite input file '%s'\n", cli_program_name, path.data);
                status = H2M_EXIT_FAILURE;
            } else {
                FILE *fp = fopen(path.data, "wb");
                if (!fp) {
                    fprintf(stderr, "%s: cannot create '%s': %s\n", cli_program_name, path.data, strerror(errno));
                    status = H2M_EXIT_FAILURE;
                } else {
                    bool ok = write_all(fp, md, md_len, path.data);
                    if (!close_output(fp, path.data) || !ok)
                        status = H2M_EXIT_FAILURE;
                }
            }
            h2m_buf_free(&path);
            h2m_free(md);
            continue;
        }

        if (!shared) {
            if (cfg.output) {
                shared = fopen(cfg.output, "wb");
                if (!shared) {
                    fprintf(stderr, "%s: cannot create '%s': %s\n", cli_program_name, cfg.output, strerror(errno));
                    h2m_free(md);
                    cli_config_free(&cfg);
                    return H2M_EXIT_FAILURE;
                }
            } else {
                shared = stdout;
            }
        }
        /* Separate consecutive documents with a blank line. */
        if (wrote_any && md_len && !write_all(shared, "\n", 1, shared_name))
            status = H2M_EXIT_FAILURE;
        if (!write_all(shared, md, md_len, shared_name))
            status = H2M_EXIT_FAILURE;
        if (md_len)
            wrote_any = true;
        h2m_free(md);
    }

    if (shared && !close_output(shared, shared_name))
        status = H2M_EXIT_FAILURE;
    cli_config_free(&cfg);
    return status;
}
