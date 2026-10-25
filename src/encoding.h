/*
 * Input decoding: byte-order-mark and <meta charset> sniffing, transcoding of
 * common legacy encodings to UTF-8, and input stream preprocessing
 * (CR/CRLF -> LF, NUL removal, invalid sequences -> U+FFFD).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_ENCODING_H
#define H2M_ENCODING_H

#include <stddef.h>
#include <stdint.h>

typedef enum h2m_encoding {
    H2M_ENC_UNKNOWN = 0,
    H2M_ENC_UTF8,
    H2M_ENC_WINDOWS_1252,
    H2M_ENC_ISO_8859_15,
    H2M_ENC_UTF16LE,
    H2M_ENC_UTF16BE
} h2m_encoding;

/* Maps a WHATWG encoding label (case-insensitive) to an encoding. */
h2m_encoding h2m_encoding_lookup(const char *label, size_t len);

/* Canonical name of an encoding, e.g. "windows-1252". */
const char *h2m_encoding_name(h2m_encoding enc);

/* Windows-1252 mapping of bytes 0x80-0x9F (other bytes map to themselves). */
uint32_t h2m_windows1252_cp(unsigned char byte);

/*
 * Decodes `len` bytes to normalized UTF-8. `forced` selects the encoding
 * (H2M_ENC_UNKNOWN = sniff). When a <meta charset> names an unsupported
 * encoding, a message is written to `warn` (if non-NULL). Returns a malloc'd,
 * NUL-terminated buffer, or NULL when memory could not be allocated.
 */
char *h2m_decode_input(const char *in, size_t len, h2m_encoding forced,
                       size_t *out_len, h2m_encoding *used,
                       char *warn, size_t warn_size);

#endif /* H2M_ENCODING_H */
