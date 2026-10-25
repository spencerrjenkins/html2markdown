/*
 * UTF-8 encoding/decoding and the small amount of Unicode character
 * classification needed for Markdown generation (whitespace, punctuation,
 * display width).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_UTF8_H
#define H2M_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define H2M_REPLACEMENT_CHAR 0xFFFDu

/* Encodes cp into out (at least 4 bytes). Returns the number of bytes. */
size_t h2m_utf8_encode(uint32_t cp, char *out);

/*
 * Decodes one code point from s (len > 0). Invalid or truncated sequences
 * decode as U+FFFD consuming one byte. *adv receives the bytes consumed.
 */
uint32_t h2m_utf8_decode(const char *s, size_t len, size_t *adv);

/* Decodes the code point that ends at s[len-1]. Returns 0 when len == 0. */
uint32_t h2m_utf8_decode_last(const char *s, size_t len);

/* Returns true when s is entirely valid UTF-8. */
bool h2m_utf8_valid(const char *s, size_t len);

/* Terminal column width of a code point: 0, 1 or 2. */
int h2m_cp_width(uint32_t cp);

/* Display width of a UTF-8 string. */
size_t h2m_utf8_width(const char *s, size_t len);

/* Unicode whitespace as defined by CommonMark (Zs plus tab, LF, FF, CR). */
bool h2m_cp_is_space(uint32_t cp);

/* Unicode punctuation as defined by CommonMark (ASCII punctuation, P*, S*). */
bool h2m_cp_is_punct(uint32_t cp);

/* ASCII punctuation: the characters that may be backslash-escaped. */
bool h2m_is_ascii_punct(int c);

/* Letters/digits for the purpose of intraword emphasis detection. */
bool h2m_cp_is_alnum(uint32_t cp);

#endif /* H2M_UTF8_H */
