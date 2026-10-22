# design-notes.md

## Architecture

```
        main.c
          |  (CLI: -f/--file is rewritten into a positional file argument)
          v
        app.c   (GtkApplication, window, sidebar, search, zoom)
          |          |
          |          v
          |   render.c (AST -> GtkTextBuffer with Pango tags)
          |
          v
     parser.c (pure GLib: Markdown -> MdNode AST, plus md_render_html)
```

### The pure-core rule

`parser.c` and `parser.h` depend only on GLib and standard C. They never include
GTK headers. This is what lets the parser be compiled and tested headlessly in
`tests/` without a display or GTK initialization.

Rendering (GTK) consumes the parser's output; the parser never touches widgets.

### Error handling

Every operation that can fail (reading a file, parsing input) reports through a
`GError *` out-param. Callers propagate rather than swallow:

```c
GError* err = NULL;
MdNode* doc = md_parse(src, &options, &err);
if (doc == NULL) {
    g_propagate_error(out, err);
    return;
}
```

### Security

- Never trust input: escape HTML with `g_markup_escape_text` before embedding
  user content into markup.
- Guard against unbounded buffers; grow `GString` incrementally instead of
  fixed-size arrays.
- Reject / skip malformed constructs instead of crashing.

### Rendering strategy

The current view is a read-only `GtkTextView`. The parser's AST is walked block
by block and inline by inline into a `GtkTextBuffer`: text is inserted plainly
and styling is applied with named `GtkTextTag`s (`md-heading1`, `md-bold`,
`md-italic`, `md-mono`, `md-codeblock`, `md-table`, …) rather than Pango markup
strings, so user content never has to be escaped into a markup language. Raw
HTML is inserted as literal text; images are anchored as child widgets.