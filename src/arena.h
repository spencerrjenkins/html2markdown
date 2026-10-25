/*
 * Bump-pointer arena allocator. All DOM nodes, attribute strings and text
 * belong to one arena and are released together.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef H2M_ARENA_H
#define H2M_ARENA_H

#include <stddef.h>

typedef struct h2m_arena_chunk h2m_arena_chunk;

typedef struct h2m_arena {
    h2m_arena_chunk *head;
} h2m_arena;

void h2m_arena_init(h2m_arena *a);
void h2m_arena_free(h2m_arena *a);

/* Returns zeroed, suitably aligned memory. Never returns NULL. */
void *h2m_arena_alloc(h2m_arena *a, size_t n);

/* Copies n bytes and appends a NUL terminator. */
char *h2m_arena_strndup(h2m_arena *a, const char *s, size_t n);

#endif /* H2M_ARENA_H */
