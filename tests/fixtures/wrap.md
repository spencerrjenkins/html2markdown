# A heading that is quite long and must never be wrapped even when narrow

Markdown files are often wrapped at seventy-two columns so that they
read comfortably in terminals and produce small diffs. The wrapper must
never break a line where the next word would start a new block, such
as - a dash, + a plus sign, 1. a number with a period, or # a hash.

> Quoted text is wrapped too, with the quote prefix accounted for in the
> available width of each line.

- List items wrap with a hanging indent that lines up under the first
  character of the item text.
- Short item.

Links like [this one with a long
URL](https://example.com/a/very/long/path/that/cannot/be/broken) keep
their destination intact, and `code spans stay whole`.

```
Code blocks are never wrapped, no matter how long their lines are, because whitespace in code is significant.
```
