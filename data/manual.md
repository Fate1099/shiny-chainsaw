# md-viewer manual

A **pure**, *lightweight* Markdown viewer for `technical docs`. This very file is
the default document: it doubles as a small manual **and** a torture test for
the parser.

> Edge cases live throughout — expect them to render.

## Quick start

Open a Markdown file with **Ctrl+O** or drag it in. Use the sidebar to jump
around, **Ctrl+F** to search, and **Ctrl+=** / **Ctrl+-** to zoom. The view
follows the file: when it changes on disk it is re-rendered, and *Open recent*
in the menu lists the last ten files.

### Keyboard shortcuts

| Key              | Action                |
|------------------|-----------------------|
| Ctrl+O           | Open a file           |
| Ctrl+F           | Search                |
| Ctrl+R           | Reload the open file  |
| Ctrl+= or Ctrl++ | Zoom in               |
| Ctrl+-           | Zoom out              |
| Ctrl+0           | Reset zoom            |
| F9               | Toggle the sidebar    |

## Features

1. Pure C parser — no external renderer, ever.
2. A single AST with feature flags for CommonMark, GFM and Markdown Extra.
3. Syntax-aware rendering via Pango.

## Blocks

### Unordered list

- bold **item**
- italic *item*
- `code item`

### Ordered list

1. First
2. Second
3. Third

### Blockquote

> "The only way to deal with an unfree world is to become so absolutely free
> that your very existence is an act of rebellion."
>
> — Albert Camus

### Fenced code

```c
#include <stdio.h>

int main(void) {
    printf("hello, md-viewer!\n");
    return 0;
}
```

### Indented code

    this line is indented four spaces
        and so is a nested indented line

### Thematic break

---

## Inline stress tests

- Escaping: `<tag>` and `a & b` and `"quotes"` and `2 < 3`.
- Strong with punctuation: **bold**, ** bold **, **bold,**.
- Emphasis: *em*, _under_, *em,*. 
- Nested: ***bold and em***.
- Code span with `<`, `&`, `"`: `x < y && z`.
- Link: [the docs](https://example.org/docs "Read the docs").
- Backslash escape: \*not emphasized\* and \# not a heading.

## Deep nesting torture

> - a list item **inside** a blockquote
> - with a `code` span and a [link](https://example.org)
>
>   continued paragraph *below* the list.

1. item with `code` and **bold**
   1. nested ordered item
   2. another

## Raw characters to survive

```
<not html>
& not an entity
" not quoted
' not apostrophe
```

## The end

That is the whole manual. If everything above renders sensibly, the parser is
doing its job.