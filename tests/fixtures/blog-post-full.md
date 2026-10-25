[Skip to content](#content)

[![The Systems Blog](/logo.svg)](/)

- [Home](/)
- [Archive](/archive)
- [About](/about)

# Understanding Memory Arenas

By [Ada Lovelace](/authors/ada) · March 14, 2026

Allocating many small objects with `malloc` is *slow* and makes cleanup error-prone. An **arena** (also called a *region*) solves both problems.

## How it works

An arena hands out memory by bumping a pointer:

```c
void *arena_alloc(Arena *a, size_t n) {
    n = (n + 15) & ~(size_t)15;   /* align to 16 bytes */
    if (a->used + n > a->cap)
        return NULL;
    void *p = a->base + a->used;
    a->used += n;
    return p;
}
```

Freeing is a single call — everything goes at once.

## Trade-offs

- **Pros:** fast, cache friendly, no leaks.
- **Cons:** no individual `free()`; memory is held until the arena is reset.

> “Premature optimization is the root of all evil.”
>
> — *Donald Knuth*

![Diagram of an arena with a bump pointer](/img/arena-diagram.png)

Figure 1: The bump pointer moves right on each allocation.

**Note:** arenas pair well with string interning.

[Share on X](https://twitter.com/intent/tweet?url=x) Copy link

## 3 Comments

### Popular posts

- [Post one](/p/1)

© 2026 The Systems Blog. [Privacy](/privacy)
