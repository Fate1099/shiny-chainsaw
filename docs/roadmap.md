# Roadmap

Tracked against the checklist in `AGENTS.md`.

## Scope

`md-viewer` targets a **single superset spec with feature flags**, not three
separate parsers. The core is **CommonMark**; **GFM** and **Markdown Extra**
are layered on as extension flags. One AST + one renderer serves all flavors —
differences live only in what the parser produces.

Flavors:

- **CommonMark** — the full base spec (blocks + inlines).
- **GFM** — CommonMark + tables, task lists, strikethrough, autolinks,
  fenced-code lang, raw-HTML filtering.
- **Markdown Extra** — tables (looser syntax), definition lists, footnotes,
  abbreviations, attribute IDs/classes.

> Note: Extra is derived from the 2004 Markdown.pl, not CommonMark, so tables
> and footnotes overlap semantically with GFM. Reuse the GFM table node for
> Extra's looser table syntax rather than duplicating it.

## Milestone 1 — Project scaffolding (done)

- [x] Meson + Ninja build for GTK3
- [x] `MdApp` GTK application with a scrolling `GtkTextView`
- [x] Pure-C parser skeleton with escaping + `<p>` wrapper
- [x] Assert-based parser unit tests wired into `meson test`

## Milestone 2 — CommonMark: block parsing

- [x] AST scaffold: `MdNode { kind, ... }`, `MdOptions` flags struct
- [x] Headings (`#`–`######`)
- [x] Paragraphs and hard line breaks
- [x] Unordered / ordered lists (incl. nesting)
- [x] Fenced and indented code blocks
- [x] Blockquotes
- [x] Thematic breaks (`---`, `***`)
- [x] HTML escaping and raw HTML passthrough policy
- [x] CommonMark `spec.txt` conformance harness wired into `meson test`

## Milestone 3 — CommonMark: inline parsing

- [x] Bold / italic / code spans
- [x] Links and images
- [x] Entity refs and character escapes
- [x] CommonMark conformance green on the inline sections

## Milestone 4 — GFM extensions

- [x] Strikethrough
- [x] Fenced-code language tags
- [x] Autolinks (www, email)
- [x] Tables
- [x] Task lists
- [x] Raw-HTML filtering policy
- [x] GFM `spec.txt` conformance harness

## Milestone 5 — Markdown Extra extensions

- [ ] Tables with Extra's looser syntax (reuse GFM table node)
- [ ] Definition lists
- [ ] Footnotes
- [ ] Abbreviations
- [ ] Attribute IDs / classes (`{:#id}`)
- [ ] Table-driven Extra tests (no official suite; write from the docs)

## Milestone 6 — Rendering

- [x] Renderer walks AST → HTML
- [x] Render parser HTML into `GtkTextBuffer` with Pango markup
- [x] Syntax-aware styling for code blocks
- [x] CSS-driven theme via GResource (`data/`)

## Milestone 7 — UX

- [x] File open dialog + CLI arg
- [ ] Recent files
- [x] Keyboard shortcuts (open, search, sidebar, zoom, quit)
- [ ] Live reload on file change

## Design notes

- Keep `parser.c` free of GTK includes; test it headlessly.
- Add `MdOptions` and the AST `kind` enum **now** — retrofitting an AST is far
  harder than growing one.
- Return `GError *` from any fallible operation; never swallow errors.
- Favor `GString` over manual buffers while building HTML.
- Use CommonMark + GFM `spec.txt` as table-driven vectors; report % passing.