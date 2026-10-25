/*
 * Command-line argument parsing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cli.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *cli_program_name = "html2md";

enum opt_id {
    OPT_OUTPUT,
    OPT_OUTPUT_DIR,
    OPT_ENCODING,
    OPT_BASE_URL,
    OPT_MAIN,
    OPT_SELECT,
    OPT_EXCLUDE,
    OPT_KEEP_HIDDEN,
    OPT_LINKS,
    OPT_IMAGES,
    OPT_KEEP_DATA_URIS,
    OPT_FLAVOR,
    OPT_HEADING_STYLE,
    OPT_BULLET,
    OPT_EMPHASIS,
    OPT_STRONG,
    OPT_CODE_STYLE,
    OPT_FENCE,
    OPT_BR_STYLE,
    OPT_WRAP,
    OPT_COMPACT_TABLES,
    OPT_NO_HTML,
    OPT_PRESERVE_NBSP,
    OPT_FRONT_MATTER,
    OPT_QUIET,
    OPT_HELP,
    OPT_VERSION
};

typedef struct {
    const char *name;
    char short_name;
    bool has_arg;
    enum opt_id id;
} optdef;

static const optdef options[] = {
    {"output", 'o', true, OPT_OUTPUT},
    {"output-dir", 'd', true, OPT_OUTPUT_DIR},
    {"encoding", 'e', true, OPT_ENCODING},
    {"base-url", 'b', true, OPT_BASE_URL},
    {"main", 'm', false, OPT_MAIN},
    {"select", 's', true, OPT_SELECT},
    {"exclude", 'x', true, OPT_EXCLUDE},
    {"keep-hidden", 0, false, OPT_KEEP_HIDDEN},
    {"links", 0, true, OPT_LINKS},
    {"images", 0, true, OPT_IMAGES},
    {"keep-data-uris", 0, false, OPT_KEEP_DATA_URIS},
    {"flavor", 0, true, OPT_FLAVOR},
    {"heading-style", 0, true, OPT_HEADING_STYLE},
    {"bullet", 0, true, OPT_BULLET},
    {"emphasis", 0, true, OPT_EMPHASIS},
    {"strong", 0, true, OPT_STRONG},
    {"code-style", 0, true, OPT_CODE_STYLE},
    {"fence", 0, true, OPT_FENCE},
    {"br-style", 0, true, OPT_BR_STYLE},
    {"wrap", 'w', true, OPT_WRAP},
    {"compact-tables", 0, false, OPT_COMPACT_TABLES},
    {"no-html", 0, false, OPT_NO_HTML},
    {"preserve-nbsp", 0, false, OPT_PRESERVE_NBSP},
    {"front-matter", 0, false, OPT_FRONT_MATTER},
    {"quiet", 'q', false, OPT_QUIET},
    {"help", 'h', false, OPT_HELP},
    {"version", 'V', false, OPT_VERSION},
};

#define NOPTIONS (sizeof options / sizeof options[0])

void cli_print_version(FILE *out)
{
    fprintf(out,
            "html2md %s\n"
            "Copyright (c) 2026 html2md contributors.\n"
            "License MIT: <https://opensource.org/license/mit>.\n"
            "This is free software: you are free to change and redistribute it.\n",
            h2m_version());
}

void cli_print_try_help(FILE *err)
{
    fprintf(err, "Try '%s --help' for more information.\n", cli_program_name);
}

void cli_print_help(FILE *out)
{
    fprintf(out, "Usage: %s [OPTION]... [FILE]...\n", cli_program_name);
    fputs(
        "Convert HTML documents to clean, readable Markdown.\n"
        "\n"
        "Reads each FILE, or standard input when FILE is '-' or absent, and writes\n"
        "Markdown to standard output. Scripts, styles, forms, hidden elements and\n"
        "other non-content markup are removed.\n"
        "\n"
        "Input and output:\n"
        "  -o, --output=FILE        write the result to FILE instead of standard output\n"
        "  -d, --output-dir=DIR     write each input to DIR/NAME.md (created if needed)\n"
        "  -e, --encoding=NAME      input character encoding: utf-8, windows-1252,\n"
        "                           iso-8859-1, iso-8859-15, utf-16le, utf-16be\n"
        "                           (default: detect from BOM / <meta charset>)\n"
        "  -b, --base-url=URL       resolve relative links and images against URL\n"
        "\n"
        "Content selection:\n"
        "  -m, --main               keep only the main content (<main>, <article>,\n"
        "                           role=main) and drop navigation, headers, footers\n"
        "                           and sidebars\n"
        "  -s, --select=SELECTOR    convert only elements matching the CSS SELECTOR\n"
        "  -x, --exclude=SELECTOR   remove elements matching SELECTOR (repeatable)\n"
        "      --keep-hidden        keep elements hidden with the hidden attribute,\n"
        "                           aria-hidden=\"true\" or display:none\n"
        "      --links=MODE         inline (default), reference, or none (keep text)\n"
        "      --images=MODE        inline (default), alt (alt text only), or none\n"
        "      --keep-data-uris     keep images embedded as data: URIs\n"
        "\n"
        "Markdown style:\n"
        "      --flavor=NAME        gfm (default) or commonmark\n"
        "      --heading-style=S    atx (default, '# Title') or setext (underlined)\n"
        "      --bullet=CHAR        bullet list marker: - (default), * or +\n"
        "      --emphasis=CHAR      emphasis delimiter: * (default) or _\n"
        "      --strong=CHAR        strong emphasis delimiter: * (default, **) or _\n"
        "      --code-style=S       fenced (default) or indented code blocks\n"
        "      --fence=CHAR         code fence character: ` (default) or ~\n"
        "      --br-style=S         hard line breaks: backslash (default) or spaces\n"
        "  -w, --wrap=COLUMNS       wrap paragraphs at COLUMNS; 0 disables (default)\n"
        "      --compact-tables     do not pad table cells to align columns\n"
        "      --no-html            never emit raw HTML (such as <sup> or <kbd>)\n"
        "      --preserve-nbsp      keep non-breaking spaces instead of plain spaces\n"
        "      --front-matter       prepend YAML front matter (title, description, ...)\n"
        "\n"
        "Other:\n"
        "  -q, --quiet              suppress warnings\n"
        "  -h, --help               display this help and exit\n"
        "  -V, --version            output version information and exit\n"
        "\n"
        "Selectors support type, #id, .class, [attr], [attr=value] (also ~= |= ^= $= *=),\n"
        ":not(), :is(), :first-child, :last-child, and the ' ', '>', '+', '~' combinators.\n"
        "\n"
        "Examples:\n",
        out);
    fprintf(out,
            "  %s page.html > page.md\n"
            "  curl -s https://example.com/ | %s -m -b https://example.com/\n"
            "  %s -s article -x '.ad, .share' --links=reference post.html\n"
            "  %s -d notes/ *.html\n"
            "\n"
            "Exit status: 0 on success, 1 if an input could not be read or an output\n"
            "could not be written, 2 on invalid usage.\n",
            cli_program_name, cli_program_name, cli_program_name, cli_program_name);
}

/* --- diagnostics --------------------------------------------------------- */

static size_t edit_distance(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la > 64 || lb > 64)
        return 99;
    size_t prev[65], cur[65];
    for (size_t j = 0; j <= lb; j++)
        prev[j] = j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = i;
        for (size_t j = 1; j <= lb; j++) {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            size_t best = prev[j] + 1;
            if (cur[j - 1] + 1 < best)
                best = cur[j - 1] + 1;
            if (prev[j - 1] + cost < best)
                best = prev[j - 1] + cost;
            cur[j] = best;
        }
        memcpy(prev, cur, (lb + 1) * sizeof prev[0]);
    }
    return prev[lb];
}

static void suggest_option(const char *name, FILE *err)
{
    const char *best = NULL;
    size_t best_d = 3;
    for (size_t i = 0; i < NOPTIONS; i++) {
        size_t d = edit_distance(name, options[i].name);
        if (d < best_d) {
            best_d = d;
            best = options[i].name;
        }
    }
    if (best)
        fprintf(err, "Did you mean '--%s'?\n", best);
}

/* --- value parsing ------------------------------------------------------- */

typedef struct {
    const char *name;
    int value;
} choice;

/* Matches `arg` against `choices` (case-insensitive, unambiguous prefixes
 * allowed). Returns false and prints a diagnostic on failure. */
static bool parse_choice(const char *opt, const char *arg, const choice *choices, size_t n, int *out, FILE *err)
{
    size_t alen = strlen(arg);
    int found = -1;
    bool ambiguous = false;
    for (size_t i = 0; i < n && alen; i++) {
        size_t cl = strlen(choices[i].name);
        bool prefix = alen <= cl;
        for (size_t k = 0; prefix && k < alen; k++) {
            if (tolower((unsigned char)arg[k]) != tolower((unsigned char)choices[i].name[k]))
                prefix = false;
        }
        if (!prefix)
            continue;
        if (alen == cl) { /* exact match wins */
            found = (int)i;
            ambiguous = false;
            break;
        }
        if (found >= 0 && choices[found].value != choices[i].value)
            ambiguous = true;
        found = (int)i;
    }
    if (found < 0 || ambiguous) {
        fprintf(err, "%s: %s value '%s' for --%s; valid values are:", cli_program_name,
                ambiguous ? "ambiguous" : "invalid", arg, opt);
        for (size_t i = 0; i < n; i++)
            fprintf(err, "%s '%s'", i ? "," : "", choices[i].name);
        fputc('\n', err);
        return false;
    }
    *out = choices[found].value;
    return true;
}

static bool parse_char_choice(const char *opt, const char *arg, const char *allowed, char *out, FILE *err)
{
    if (strlen(arg) == 1 && strchr(allowed, arg[0])) {
        *out = arg[0];
        return true;
    }
    /* Accept the doubled form for --strong ("**" / "__"). */
    if (strlen(arg) == 2 && arg[0] == arg[1] && strchr(allowed, arg[0]) && strcmp(opt, "strong") == 0) {
        *out = arg[0];
        return true;
    }
    fprintf(err, "%s: invalid value '%s' for --%s; valid values are:", cli_program_name, arg, opt);
    for (const char *p = allowed; *p; p++)
        fprintf(err, "%s '%c'", p == allowed ? "" : ",", *p);
    fputc('\n', err);
    return false;
}

static bool parse_wrap(const char *arg, int *out, FILE *err)
{
    char *end;
    errno = 0;
    long v = strtol(arg, &end, 10);
    if (errno || end == arg || *end || v < 0 || v > 10000 || (v > 0 && v < 10)) {
        fprintf(err, "%s: invalid wrap width '%s'; use 0 (no wrapping) or a column between 10 and 10000\n",
                cli_program_name, arg);
        return false;
    }
    *out = (int)v;
    return true;
}

static bool has_scheme(const char *url)
{
    if (!isalpha((unsigned char)url[0]))
        return false;
    const char *p = url + 1;
    while (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')
        p++;
    return *p == ':';
}

/* Applies one option. Returns false on a usage error. */
static bool apply(cli_config *cfg, const optdef *o, const char *arg, bool *done, FILE *out, FILE *err)
{
    h2m_options *opts = &cfg->opts;
    int v;
    switch (o->id) {
    case OPT_OUTPUT:
        cfg->output = arg;
        return true;
    case OPT_OUTPUT_DIR:
        cfg->output_dir = arg;
        return true;
    case OPT_ENCODING:
        if (!h2m_encoding_is_supported(arg)) {
            fprintf(err,
                    "%s: unsupported encoding '%s'; supported: utf-8, windows-1252, iso-8859-1, "
                    "iso-8859-15, us-ascii, utf-16le, utf-16be\n",
                    cli_program_name, arg);
            return false;
        }
        opts->encoding = arg;
        return true;
    case OPT_BASE_URL:
        if (!has_scheme(arg)) {
            fprintf(err, "%s: base URL '%s' must be absolute (for example https://example.com/)\n",
                    cli_program_name, arg);
            return false;
        }
        opts->base_url = arg;
        return true;
    case OPT_MAIN:
        opts->main_content = true;
        return true;
    case OPT_SELECT: {
        char msg[200];
        if (!h2m_selector_is_valid(arg, msg, sizeof msg)) {
            fprintf(err, "%s: invalid selector '%s': %s\n", cli_program_name, arg, msg);
            return false;
        }
        opts->select = arg;
        return true;
    }
    case OPT_EXCLUDE: {
        char msg[200];
        if (!h2m_selector_is_valid(arg, msg, sizeof msg)) {
            fprintf(err, "%s: invalid selector '%s': %s\n", cli_program_name, arg, msg);
            return false;
        }
        if (cfg->exclude.len)
            h2m_buf_puts(&cfg->exclude, ", ");
        h2m_buf_puts(&cfg->exclude, arg);
        return true;
    }
    case OPT_KEEP_HIDDEN:
        opts->keep_hidden = true;
        return true;
    case OPT_LINKS: {
        static const choice c[] = {{"inline", H2M_LINKS_INLINE}, {"reference", H2M_LINKS_REFERENCE},
                                   {"none", H2M_LINKS_TEXT}};
        if (!parse_choice(o->name, arg, c, 3, &v, err))
            return false;
        opts->link_style = (h2m_link_style)v;
        return true;
    }
    case OPT_IMAGES: {
        static const choice c[] = {{"inline", H2M_IMAGES_INLINE}, {"alt", H2M_IMAGES_ALT}, {"none", H2M_IMAGES_NONE}};
        if (!parse_choice(o->name, arg, c, 3, &v, err))
            return false;
        opts->image_style = (h2m_image_style)v;
        return true;
    }
    case OPT_KEEP_DATA_URIS:
        opts->keep_data_uris = true;
        return true;
    case OPT_FLAVOR: {
        static const choice c[] = {{"gfm", H2M_FLAVOR_GFM}, {"commonmark", H2M_FLAVOR_COMMONMARK}};
        if (!parse_choice(o->name, arg, c, 2, &v, err))
            return false;
        opts->flavor = (h2m_flavor)v;
        return true;
    }
    case OPT_HEADING_STYLE: {
        static const choice c[] = {{"atx", H2M_HEADING_ATX}, {"setext", H2M_HEADING_SETEXT}};
        if (!parse_choice(o->name, arg, c, 2, &v, err))
            return false;
        opts->heading_style = (h2m_heading_style)v;
        return true;
    }
    case OPT_CODE_STYLE: {
        static const choice c[] = {{"fenced", H2M_CODE_FENCED}, {"indented", H2M_CODE_INDENTED}};
        if (!parse_choice(o->name, arg, c, 2, &v, err))
            return false;
        opts->code_style = (h2m_code_style)v;
        return true;
    }
    case OPT_BR_STYLE: {
        static const choice c[] = {{"backslash", H2M_BR_BACKSLASH}, {"spaces", H2M_BR_SPACES}};
        if (!parse_choice(o->name, arg, c, 2, &v, err))
            return false;
        opts->br_style = (h2m_br_style)v;
        return true;
    }
    case OPT_BULLET:
        return parse_char_choice(o->name, arg, "-*+", &opts->bullet, err);
    case OPT_EMPHASIS:
        return parse_char_choice(o->name, arg, "*_", &opts->emphasis, err);
    case OPT_STRONG:
        return parse_char_choice(o->name, arg, "*_", &opts->strong, err);
    case OPT_FENCE:
        return parse_char_choice(o->name, arg, "`~", &opts->fence, err);
    case OPT_WRAP:
        return parse_wrap(arg, &opts->wrap, err);
    case OPT_COMPACT_TABLES:
        opts->compact_tables = true;
        return true;
    case OPT_NO_HTML:
        opts->raw_html = false;
        return true;
    case OPT_PRESERVE_NBSP:
        opts->preserve_nbsp = true;
        return true;
    case OPT_FRONT_MATTER:
        opts->front_matter = true;
        return true;
    case OPT_QUIET:
        cfg->quiet = true;
        return true;
    case OPT_HELP:
        cli_print_help(out);
        *done = true;
        return true;
    case OPT_VERSION:
        cli_print_version(out);
        *done = true;
        return true;
    }
    return false;
}

/* Finds a long option by exact name or unambiguous prefix. */
static const optdef *find_long(const char *name, size_t len, FILE *err)
{
    const optdef *match = NULL;
    size_t nmatch = 0;
    for (size_t i = 0; i < NOPTIONS; i++) {
        if (strlen(options[i].name) == len && strncmp(options[i].name, name, len) == 0)
            return &options[i];
        if (strncmp(options[i].name, name, len) == 0) {
            match = &options[i];
            nmatch++;
        }
    }
    if (nmatch == 1)
        return match;
    if (nmatch > 1) {
        fprintf(err, "%s: option '--%.*s' is ambiguous; possibilities:", cli_program_name, (int)len, name);
        for (size_t i = 0; i < NOPTIONS; i++) {
            if (strncmp(options[i].name, name, len) == 0)
                fprintf(err, " '--%s'", options[i].name);
        }
        fputc('\n', err);
        return NULL;
    }
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%.*s", (int)(len < 100 ? len : 100), name);
    fprintf(err, "%s: unrecognized option '--%s'\n", cli_program_name, tmp);
    suggest_option(tmp, err);
    return NULL;
}

static const optdef *find_short(char c)
{
    for (size_t i = 0; i < NOPTIONS; i++) {
        if (options[i].short_name == c)
            return &options[i];
    }
    return NULL;
}

void cli_config_free(cli_config *cfg)
{
    free(cfg->inputs);
    cfg->inputs = NULL;
    cfg->ninputs = 0;
    h2m_buf_free(&cfg->exclude);
}

cli_result cli_parse(int argc, char **argv, cli_config *cfg, FILE *out, FILE *err)
{
    memset(cfg, 0, sizeof *cfg);
    h2m_options_init(&cfg->opts);
    h2m_buf_init(&cfg->exclude);
    cfg->inputs = h2m_xcalloc((size_t)(argc > 0 ? argc : 1), sizeof *cfg->inputs);

    bool done = false;
    bool only_files = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (only_files || a[0] != '-' || strcmp(a, "-") == 0) {
            cfg->inputs[cfg->ninputs++] = a;
            continue;
        }
        if (strcmp(a, "--") == 0) {
            only_files = true;
            continue;
        }
        if (a[1] == '-') {
            const char *name = a + 2;
            const char *eq = strchr(name, '=');
            size_t len = eq ? (size_t)(eq - name) : strlen(name);
            const optdef *o = find_long(name, len, err);
            if (!o) {
                cli_print_try_help(err);
                return CLI_USAGE_ERROR;
            }
            const char *arg = NULL;
            if (o->has_arg) {
                if (eq) {
                    arg = eq + 1;
                } else if (i + 1 < argc) {
                    arg = argv[++i];
                } else {
                    fprintf(err, "%s: option '--%s' requires an argument\n", cli_program_name, o->name);
                    cli_print_try_help(err);
                    return CLI_USAGE_ERROR;
                }
            } else if (eq) {
                fprintf(err, "%s: option '--%s' doesn't allow an argument\n", cli_program_name, o->name);
                cli_print_try_help(err);
                return CLI_USAGE_ERROR;
            }
            if (!apply(cfg, o, arg, &done, out, err)) {
                cli_print_try_help(err);
                return CLI_USAGE_ERROR;
            }
            if (done)
                return CLI_DONE;
            continue;
        }
        /* Cluster of short options. */
        for (const char *p = a + 1; *p; p++) {
            const optdef *o = find_short(*p);
            if (!o) {
                fprintf(err, "%s: invalid option -- '%c'\n", cli_program_name, *p);
                cli_print_try_help(err);
                return CLI_USAGE_ERROR;
            }
            const char *arg = NULL;
            if (o->has_arg) {
                if (p[1]) {
                    arg = p + 1;
                } else if (i + 1 < argc) {
                    arg = argv[++i];
                } else {
                    fprintf(err, "%s: option requires an argument -- '%c'\n", cli_program_name, *p);
                    cli_print_try_help(err);
                    return CLI_USAGE_ERROR;
                }
            }
            if (!apply(cfg, o, arg, &done, out, err)) {
                cli_print_try_help(err);
                return CLI_USAGE_ERROR;
            }
            if (done)
                return CLI_DONE;
            if (arg)
                break;
        }
    }

    if (cfg->output && cfg->output_dir) {
        fprintf(err, "%s: --output and --output-dir cannot be used together\n", cli_program_name);
        cli_print_try_help(err);
        return CLI_USAGE_ERROR;
    }
    if (cfg->output_dir) {
        for (size_t k = 0; k < cfg->ninputs; k++) {
            if (strcmp(cfg->inputs[k], "-") == 0) {
                fprintf(err, "%s: --output-dir needs named input files (standard input has no name)\n",
                        cli_program_name);
                cli_print_try_help(err);
                return CLI_USAGE_ERROR;
            }
        }
        if (cfg->ninputs == 0) {
            fprintf(err, "%s: --output-dir needs at least one input file\n", cli_program_name);
            cli_print_try_help(err);
            return CLI_USAGE_ERROR;
        }
    }
    if (cfg->exclude.len)
        cfg->opts.exclude = cfg->exclude.data;
    return CLI_RUN;
}
