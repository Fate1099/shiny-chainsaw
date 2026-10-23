# GFM Capability Demonstrations

Open this file in Shiny Chainsaw to visually verify the GFM extensions. Each
section shows the expected result next to the raw Markdown.

## Strikethrough

`~single~` and `~~double~~` both strike:

- ~delete me~
- ~~delete me too~~
- Inline: this is ~~bad~~ good, and ~almost~ right.

Three or more tildes stay **literal** (not struck):

- `~~~x~~~` renders as ~~~x~~~
- Unclosed: `~~open` renders as ~~open

Interactions:

- Nested emphasis: ~~*struck em*~~ and ~~**struck strong**~~
- Inside a link: [~~struck link~~](https://example.com/foo)

## Task lists

Standard tasks with `[ ]`, `[x]`, `[X]`:

- [ ] unchecked item
- [x] checked item
- [X] uppercase checked item

Nested tasks:

- [x] high-level done
  - [ ] subtask one
  - [x] subtask two
- [ ] another top-level

Task with formatted content:

- [x] **bold** item with `code` and a [link](https://example.com)
- [ ] item with *emphasis* and trailing text

**Not** task markers — these stay plain bullets:

- [x]no space
- [  ] two spaces
- [correct] quiz-style item
- [wrong] another quiz item

Ordered list with a task:

1. [ ] ordered task item
2. normal ordered item

## Autolinks

Bare URLs, www., email, and protocols:

- www.example.com
- https://example.com/path?q=1&x=2
- http://example.com
- user@example.com
- mailto:user@example.com
- xmpp:user@example.com

Context rules — recognized after whitespace, `(`, `*`, `_`, `~`:

- Visit www.example.com/help for docs.
- (https://encrypted.example.com/search?q=Markup+(business))
- *www.example.com/star*
- www.example.com with a trailing period.
- Parens: www.example.com/a_(b) stays inside the link.

**Not** autolinks:

- ftp://example.com (ftp scheme is not in the GFM spec list)
- www.example.com<y
- `www.example.com` (inside a code span)

## Tables

Aliases with alignment:

| Term | Description | Example |
|:-----|:-----------:|--------:|
| ATX | heading syntax | `# Title` |
| GFM | GitHub Flavored Markdown | `~~x~~` |
| BBBB | wide | y |

Autolinks inside table cells:

| Scheme | Sample |
|--------|--------|
| http | http://example.com |
| mail | user@example.com |

## Regression: core CommonMark still works

# Heading one

## Heading two

Paragraph with **bold**, *italic*, `code`, and [a link](https://example.com).

> A noble blockquote with **bold** text.

1. ordered
2. list
   - nested unordered
   - another

```c
int main(void) { return 0; }
```

---

Line ending above is a thematic break.