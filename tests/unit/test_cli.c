/*
 * Command-line parsing tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include "cli.h"
#include "testlib.h"

/* Parses a NULL-terminated argument list; diagnostics go to a temp file
 * whose contents are returned in *diag (caller frees). */
static cli_result parse(cli_config *cfg, char **diag, const char *first, ...)
{
    char *argv[64];
    int argc = 0;
    argv[argc++] = (char *)(uintptr_t) "html2md";
    va_list ap;
    va_start(ap, first);
    for (const char *a = first; a && argc < 63; a = va_arg(ap, const char *))
        argv[argc++] = (char *)(uintptr_t)a;
    va_end(ap);
    argv[argc] = NULL;

    FILE *out = tmpfile();
    FILE *err = tmpfile();
    cli_result r = cli_parse(argc, argv, cfg, out, err);
    if (diag) {
        long n = ftell(err);
        rewind(err);
        *diag = calloc(1, (size_t)(n > 0 ? n : 0) + 1);
        if (n > 0)
            fread(*diag, 1, (size_t)n, err);
    }
    fclose(out);
    fclose(err);
    return r;
}

static void test_defaults_and_files(void)
{
    cli_config c;
    CHECK(parse(&c, NULL, NULL) == CLI_RUN);
    CHECK_EQ_INT(0, c.ninputs);
    CHECK(c.opts.flavor == H2M_FLAVOR_GFM && c.opts.bullet == '-' && c.opts.raw_html);
    cli_config_free(&c);

    CHECK(parse(&c, NULL, "a.html", "-", "b.html", NULL) == CLI_RUN);
    CHECK_EQ_INT(3, c.ninputs);
    CHECK_EQ_STR("-", c.inputs[1]);
    cli_config_free(&c);

    CHECK(parse(&c, NULL, "--", "-m", "--help", NULL) == CLI_RUN);
    CHECK_EQ_INT(2, c.ninputs);
    CHECK_EQ_STR("-m", c.inputs[0]);
    CHECK(!c.opts.main_content);
    cli_config_free(&c);
}

static void test_options(void)
{
    cli_config c;
    CHECK(parse(&c, NULL, "-o", "out.md", "--base-url=https://x.org/", "-mq", "--wrap", "72", "in.html", NULL) ==
          CLI_RUN);
    CHECK_EQ_STR("out.md", c.output);
    CHECK_EQ_STR("https://x.org/", c.opts.base_url);
    CHECK(c.opts.main_content && c.quiet);
    CHECK_EQ_INT(72, c.opts.wrap);
    cli_config_free(&c);

    CHECK(parse(&c, NULL, "-oout.md", "-w80", "-x", ".ad", "--exclude=nav", "--select", "article", NULL) == CLI_RUN);
    CHECK_EQ_STR("out.md", c.output);
    CHECK_EQ_INT(80, c.opts.wrap);
    CHECK_EQ_STR(".ad, nav", c.opts.exclude);
    CHECK_EQ_STR("article", c.opts.select);
    cli_config_free(&c);

    CHECK(parse(&c, NULL, "--flavor=commonmark", "--heading-style", "setext", "--bullet=*", "--emphasis=_",
                "--strong=__", "--code-style=indented", "--fence=~", "--br-style=spaces", "--links=reference",
                "--images=alt", "--no-html", "--compact-tables", "--preserve-nbsp", "--front-matter",
                "--keep-hidden", "--keep-data-uris", "-e", "latin1", NULL) == CLI_RUN);
    CHECK(c.opts.flavor == H2M_FLAVOR_COMMONMARK);
    CHECK(c.opts.heading_style == H2M_HEADING_SETEXT);
    CHECK(c.opts.bullet == '*' && c.opts.emphasis == '_' && c.opts.strong == '_' && c.opts.fence == '~');
    CHECK(c.opts.code_style == H2M_CODE_INDENTED && c.opts.br_style == H2M_BR_SPACES);
    CHECK(c.opts.link_style == H2M_LINKS_REFERENCE && c.opts.image_style == H2M_IMAGES_ALT);
    CHECK(!c.opts.raw_html && c.opts.compact_tables && c.opts.preserve_nbsp && c.opts.front_matter);
    CHECK(c.opts.keep_hidden && c.opts.keep_data_uris);
    CHECK_EQ_STR("latin1", c.opts.encoding);
    cli_config_free(&c);

    /* Abbreviations and value prefixes. */
    CHECK(parse(&c, NULL, "--fla=common", "--links=no", "--front", NULL) == CLI_RUN);
    CHECK(c.opts.flavor == H2M_FLAVOR_COMMONMARK && c.opts.link_style == H2M_LINKS_TEXT && c.opts.front_matter);
    cli_config_free(&c);

    CHECK(parse(&c, NULL, "--images=NONE", "-w", "0", NULL) == CLI_RUN);
    CHECK(c.opts.image_style == H2M_IMAGES_NONE && c.opts.wrap == 0);
    cli_config_free(&c);

    CHECK(parse(&c, NULL, "-d", "outdir", "a.html", "b.htm", NULL) == CLI_RUN);
    CHECK_EQ_STR("outdir", c.output_dir);
    cli_config_free(&c);
}

static void test_help_version(void)
{
    cli_config c;
    CHECK(parse(&c, NULL, "--help", NULL) == CLI_DONE);
    cli_config_free(&c);
    CHECK(parse(&c, NULL, "-V", NULL) == CLI_DONE);
    cli_config_free(&c);
    CHECK(parse(&c, NULL, "-qh", NULL) == CLI_DONE);
    cli_config_free(&c);
}

static void expect_usage_error(const char *arg1, const char *arg2, const char *arg3, const char *needle)
{
    cli_config c;
    char *diag = NULL;
    cli_result r = parse(&c, &diag, arg1, arg2, arg3, NULL);
    CHECK(r == CLI_USAGE_ERROR);
    if (!diag || !strstr(diag, needle))
        tl_fail(__FILE__, __LINE__, "diagnostic for '%s %s' lacks '%s': %s", arg1, arg2 ? arg2 : "", needle,
                diag ? diag : "(none)");
    else
        tl_checks++;
    CHECK(diag && strstr(diag, "--help") != NULL);
    free(diag);
    cli_config_free(&c);
}

static void test_errors(void)
{
    expect_usage_error("--bogus", NULL, NULL, "unrecognized option '--bogus'");
    expect_usage_error("--wrpa=3", NULL, NULL, "Did you mean '--wrap'?");
    expect_usage_error("--co", NULL, NULL, "ambiguous");
    expect_usage_error("-z", NULL, NULL, "invalid option -- 'z'");
    expect_usage_error("-o", NULL, NULL, "requires an argument");
    expect_usage_error("--output", NULL, NULL, "requires an argument");
    expect_usage_error("--main=yes", NULL, NULL, "doesn't allow an argument");
    expect_usage_error("--flavor=markdown", NULL, NULL, "invalid value 'markdown' for --flavor");
    expect_usage_error("--bullet=x", NULL, NULL, "invalid value 'x' for --bullet");
    expect_usage_error("--wrap=5", NULL, NULL, "invalid wrap width");
    expect_usage_error("--wrap=abc", NULL, NULL, "invalid wrap width");
    expect_usage_error("-w", "-3", NULL, "invalid wrap width");
    expect_usage_error("--encoding=ebcdic", NULL, NULL, "unsupported encoding 'ebcdic'");
    expect_usage_error("--base-url=/relative", NULL, NULL, "must be absolute");
    expect_usage_error("--select=div >", NULL, NULL, "invalid selector");
    expect_usage_error("-x", "[oops", NULL, "invalid selector");
    expect_usage_error("-o", "a.md", "-d", "requires an argument");
    expect_usage_error("-d", "out", NULL, "needs at least one input file");
    expect_usage_error("-d", "out", "-", "standard input has no name");
    expect_usage_error("--links=", NULL, NULL, "invalid value '' for --links");
}

void suite_cli(void)
{
    test_defaults_and_files();
    test_options();
    test_help_version();
    test_errors();

    cli_config c;
    char *diag = NULL;
    CHECK(parse(&c, &diag, "-o", "a.md", "-d", "dir", "x.html", NULL) == CLI_USAGE_ERROR);
    CHECK(diag && strstr(diag, "cannot be used together"));
    free(diag);
    cli_config_free(&c);
}
