# design-notes.md

## Architecture

```
        main.c
          |  (CLI parsing: GOptionContext, -f/--file)
          v
        app.c   (GTK application, window, GtkTextView)
          |          |
          |          v
          |   parser.c (pure C: Markdown -> HTML/state)
          |
          v
   GtkTextBuffer (Pango markup) rendered in GtkTextView
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
gchar *html = md_parser_parse(src, &err);
if (html == NULL) {
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

The current view is a read-only `GtkTextView`. HTML produced by the parser is
translated into a `GtkTextBuffer` via `gtk_text_buffer_insert_markup` (which
uses Pango markup, not raw HTML), or by walking the parser's inline AST.
A Pango-marker-based mapping keeps styling (bold, italic, monospace) syntax-aware.