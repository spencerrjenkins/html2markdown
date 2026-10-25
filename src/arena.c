/*
 * Bump-pointer arena allocator.
 *
 * SPDX-License-Identifier: MIT
 */
#include "arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"

#define ARENA_ALIGN 16u
#define ARENA_CHUNK_SIZE (64u * 1024u)

struct h2m_arena_chunk {
    h2m_arena_chunk *next;
    size_t used;
    size_t cap;
    /* Payload follows, aligned to ARENA_ALIGN. */
};

static size_t header_size(void)
{
    return (sizeof(h2m_arena_chunk) + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
}

void h2m_arena_init(h2m_arena *a)
{
    a->head = NULL;
}

void h2m_arena_free(h2m_arena *a)
{
    h2m_arena_chunk *c = a->head;
    while (c) {
        h2m_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    a->head = NULL;
}

void *h2m_arena_alloc(h2m_arena *a, size_t n)
{
    if (n > SIZE_MAX - ARENA_ALIGN)
        h2m_oom();
    n = (n + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
    if (n == 0)
        n = ARENA_ALIGN;

    h2m_arena_chunk *c = a->head;
    if (!c || c->cap - c->used < n) {
        size_t cap = n > ARENA_CHUNK_SIZE / 4 ? n : ARENA_CHUNK_SIZE;
        if (cap > SIZE_MAX - header_size())
            h2m_oom();
        h2m_arena_chunk *fresh = h2m_xmalloc(header_size() + cap);
        fresh->used = 0;
        fresh->cap = cap;
        if (c && cap != ARENA_CHUNK_SIZE) {
            /* Oversized request: keep the current chunk at the head so its
             * remaining space is still used for small allocations. */
            fresh->next = c->next;
            c->next = fresh;
            fresh->used = n;
            void *big = (char *)fresh + header_size();
            memset(big, 0, n);
            return big;
        }
        fresh->next = c;
        a->head = fresh;
        c = fresh;
    }
    void *p = (char *)c + header_size() + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

char *h2m_arena_strndup(h2m_arena *a, const char *s, size_t n)
{
    char *p = h2m_arena_alloc(a, n + 1);
    if (n)
        memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
