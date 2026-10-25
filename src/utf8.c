/*
 * UTF-8 helpers and Unicode character classification.
 *
 * SPDX-License-Identifier: MIT
 */
#include "utf8.h"

size_t h2m_utf8_encode(uint32_t cp, char *out)
{
    unsigned char *o = (unsigned char *)out;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = H2M_REPLACEMENT_CHAR;
    if (cp < 0x80) {
        o[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (unsigned char)(0xC0 | (cp >> 6));
        o[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (unsigned char)(0xE0 | (cp >> 12));
        o[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (unsigned char)(0xF0 | (cp >> 18));
    o[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

uint32_t h2m_utf8_decode(const char *s, size_t len, size_t *adv)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    *adv = 1;
    if (c < 0x80)
        return c;

    size_t need;
    uint32_t cp, min;
    if (c >= 0xC2 && c <= 0xDF) {
        need = 1;
        cp = c & 0x1Fu;
        min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        need = 2;
        cp = c & 0x0Fu;
        min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 3;
        cp = c & 0x07u;
        min = 0x10000;
    } else {
        return H2M_REPLACEMENT_CHAR;
    }
    if (len < need + 1)
        return H2M_REPLACEMENT_CHAR;
    for (size_t i = 1; i <= need; i++) {
        if ((p[i] & 0xC0) != 0x80)
            return H2M_REPLACEMENT_CHAR;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return H2M_REPLACEMENT_CHAR;
    *adv = need + 1;
    return cp;
}

uint32_t h2m_utf8_decode_last(const char *s, size_t len)
{
    if (len == 0)
        return 0;
    size_t start = len - 1;
    size_t back = 0;
    while (start > 0 && back < 3 && (((unsigned char)s[start]) & 0xC0) == 0x80) {
        start--;
        back++;
    }
    size_t adv;
    uint32_t cp = h2m_utf8_decode(s + start, len - start, &adv);
    if (start + adv != len)
        return H2M_REPLACEMENT_CHAR;
    return cp;
}

bool h2m_utf8_valid(const char *s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        size_t adv;
        uint32_t cp = h2m_utf8_decode(s + i, len - i, &adv);
        if (cp == H2M_REPLACEMENT_CHAR && adv == 1)
            return false;
        i += adv;
    }
    return true;
}

typedef struct {
    uint32_t lo, hi;
} range;

static bool in_ranges(uint32_t cp, const range *r, size_t n)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < r[mid].lo)
            hi = mid;
        else if (cp > r[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

static const range zero_width[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E},
    {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x202A, 0x202E},
    {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF},
};

static const range wide[] = {
    {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},   {0x23E9, 0x23EC},
    {0x23F0, 0x23F0},   {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
    {0x2648, 0x2653},   {0x267F, 0x267F},   {0x2693, 0x2693},   {0x26A1, 0x26A1},
    {0x26AA, 0x26AB},   {0x26BD, 0x26BE},   {0x26C4, 0x26C5},   {0x26CE, 0x26CE},
    {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
    {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},
    {0x2728, 0x2728},   {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},
    {0x2757, 0x2757},   {0x2795, 0x2797},   {0x27B0, 0x27B0},   {0x27BF, 0x27BF},
    {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},   {0x2E80, 0x303E},
    {0x3041, 0x33FF},   {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},
    {0xA960, 0xA97F},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},   {0x16FE0, 0x16FE4},
    {0x17000, 0x18AFF}, {0x1B000, 0x1B2FF}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF},
    {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F251}, {0x1F300, 0x1F64F},
    {0x1F680, 0x1F6FF}, {0x1F7E0, 0x1F7EB}, {0x1F90C, 0x1F9FF}, {0x1FA70, 0x1FAFF},
    {0x20000, 0x3FFFD},
};

int h2m_cp_width(uint32_t cp)
{
    if (cp == 0)
        return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
        return 0;
    if (cp < 0x300)
        return 1;
    if (in_ranges(cp, zero_width, sizeof zero_width / sizeof zero_width[0]))
        return 0;
    if (in_ranges(cp, wide, sizeof wide / sizeof wide[0]))
        return 2;
    return 1;
}

size_t h2m_utf8_width(const char *s, size_t len)
{
    size_t w = 0, i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            w += (c >= 0x20 && c != 0x7F) ? 1u : 0u;
            i++;
            continue;
        }
        size_t adv;
        uint32_t cp = h2m_utf8_decode(s + i, len - i, &adv);
        w += (size_t)h2m_cp_width(cp);
        i += adv;
    }
    return w;
}

bool h2m_cp_is_space(uint32_t cp)
{
    switch (cp) {
    case ' ': case '\t': case '\n': case '\f': case '\r':
    case 0x00A0: case 0x1680: case 0x202F: case 0x205F: case 0x3000:
        return true;
    default:
        return cp >= 0x2000 && cp <= 0x200A;
    }
}

bool h2m_is_ascii_punct(int c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/* Approximation of Unicode general categories P* and S* outside ASCII. */
static const range punct[] = {
    {0x00A1, 0x00A9}, {0x00AB, 0x00AC}, {0x00AE, 0x00B1}, {0x00B4, 0x00B4},
    {0x00B6, 0x00B8}, {0x00BB, 0x00BB}, {0x00BF, 0x00BF}, {0x00D7, 0x00D7},
    {0x00F7, 0x00F7}, {0x02C2, 0x02C5}, {0x02D2, 0x02DF}, {0x037E, 0x037E},
    {0x0387, 0x0387}, {0x055A, 0x055F}, {0x0589, 0x058A}, {0x05BE, 0x05BE},
    {0x05C0, 0x05C0}, {0x05C3, 0x05C3}, {0x05C6, 0x05C6}, {0x05F3, 0x05F4},
    {0x0609, 0x060D}, {0x061B, 0x061B}, {0x061D, 0x061F}, {0x066A, 0x066D},
    {0x06D4, 0x06D4}, {0x0964, 0x0965}, {0x0970, 0x0970}, {0x0E3F, 0x0E3F},
    {0x0E4F, 0x0E4F}, {0x0E5A, 0x0E5B}, {0x10FB, 0x10FB}, {0x1360, 0x1368},
    {0x166E, 0x166E}, {0x16EB, 0x16ED}, {0x17D4, 0x17D6}, {0x17D8, 0x17DB},
    {0x1800, 0x180A}, {0x2010, 0x2027}, {0x2030, 0x205E}, {0x207A, 0x207E},
    {0x208A, 0x208E}, {0x20A0, 0x20C0}, {0x2100, 0x2101}, {0x2103, 0x2106},
    {0x2108, 0x2109}, {0x2114, 0x2114}, {0x2116, 0x2118}, {0x211E, 0x2123},
    {0x2125, 0x2125}, {0x2127, 0x2127}, {0x2129, 0x2129}, {0x212E, 0x212E},
    {0x213A, 0x213B}, {0x2140, 0x2144}, {0x214A, 0x214D}, {0x214F, 0x214F},
    {0x218A, 0x218B}, {0x2190, 0x2426}, {0x2440, 0x244A}, {0x249C, 0x24E9},
    {0x2500, 0x2775}, {0x2794, 0x2BFF}, {0x2CE5, 0x2CEA}, {0x2CF9, 0x2CFC},
    {0x2CFE, 0x2CFF}, {0x2E00, 0x2E7F}, {0x2E80, 0x2FFF}, {0x3001, 0x3004},
    {0x3008, 0x3020}, {0x3030, 0x3030}, {0x3036, 0x3037}, {0x303D, 0x303F},
    {0x309B, 0x309C}, {0x30A0, 0x30A0}, {0x30FB, 0x30FB}, {0xA4FE, 0xA4FF},
    {0xA60D, 0xA60F}, {0xA673, 0xA673}, {0xA67E, 0xA67E}, {0xA700, 0xA716},
    {0xA720, 0xA721}, {0xA789, 0xA78A}, {0xFB29, 0xFB29}, {0xFD3E, 0xFD3F},
    {0xFE10, 0xFE19}, {0xFE30, 0xFE52}, {0xFE54, 0xFE66}, {0xFE68, 0xFE6B},
    {0xFF01, 0xFF0F}, {0xFF1A, 0xFF20}, {0xFF3B, 0xFF40}, {0xFF5B, 0xFF65},
    {0xFFE0, 0xFFE6}, {0xFFE8, 0xFFEE}, {0xFFFC, 0xFFFD}, {0x1F000, 0x1FAFF},
};

bool h2m_cp_is_punct(uint32_t cp)
{
    if (cp < 0x80)
        return h2m_is_ascii_punct((int)cp);
    return in_ranges(cp, punct, sizeof punct / sizeof punct[0]);
}

bool h2m_cp_is_alnum(uint32_t cp)
{
    if (cp < 0x80)
        return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    return !h2m_cp_is_space(cp) && !h2m_cp_is_punct(cp);
}
