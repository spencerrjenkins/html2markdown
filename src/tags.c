/*
 * HTML tag table.
 *
 * SPDX-License-Identifier: MIT
 */
#include "tags.h"

#include <string.h>

struct tag_info {
    const char *name;
    unsigned char len;
    unsigned flags;
};

static const struct tag_info tag_table[TAG_COUNT_] = {
    {"", 0, 0},
#define H2M_TAG_ROW(id, name, flags) {name, (unsigned char)(sizeof(name) - 1), flags},
    H2M_TAG_LIST(H2M_TAG_ROW)
#undef H2M_TAG_ROW
};

h2m_tag h2m_tag_lookup(const char *name, size_t len)
{
    size_t lo = 1, hi = TAG_COUNT_;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const struct tag_info *t = &tag_table[mid];
        size_t n = len < t->len ? len : t->len;
        int c = memcmp(name, t->name, n);
        if (c == 0)
            c = (len > t->len) - (len < t->len);
        if (c == 0)
            return (h2m_tag)mid;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return TAG_UNKNOWN;
}

const char *h2m_tag_name(h2m_tag tag)
{
    if ((unsigned)tag >= TAG_COUNT_)
        return "";
    return tag_table[tag].name;
}

unsigned h2m_tag_flags(h2m_tag tag)
{
    if ((unsigned)tag >= TAG_COUNT_)
        return 0;
    return tag_table[tag].flags;
}

bool h2m_tag_table_sorted(void)
{
    for (size_t i = 2; i < TAG_COUNT_; i++) {
        if (strcmp(tag_table[i - 1].name, tag_table[i].name) >= 0)
            return false;
    }
    return true;
}
