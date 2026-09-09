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

- [ ] AST scaffold: `MdNode { kind, ... }`, `MdOptions` flags struct
- [ ] Headings (`#`–`######`)
- [ ] Paragraphs and hard line breaks
- [ ] Unordered / ordered lists (incl. nesting)
- [ ] Fenced and indented code blocks
- [ ] Blockquotes
- [ ] Thematic breaks (`---`, `***`)
- [ ] HTML escaping and raw HTML passthrough policy
- [ ] CommonMark `spec.txt` conformance harness wired into `meson test`

## Milestone 3 — CommonMark: inline parsing

- [ ] Bold / italic / code spans
- [ ] Links and images
- [ ] Entity refs and character escapes
- [ ] CommonMark conformance green on the inline sections

## Milestone 4 — GFM extensions

- [ ] Strikethrough
- [ ] Fenced-code language tags
- [ ] Autolinks (www, email)
- [ ] Tables
- [ ] Task lists
- [ ] Raw-HTML filtering policy
- [ ] GFM `spec.txt` conformance harness

## Milestone 5 — Markdown Extra extensions

- [ ] Tables with Extra's looser syntax (reuse GFM table node)
- [ ] Definition lists
- [ ] Footnotes
- [ ] Abbreviations
- [ ] Attribute IDs / classes (`{:#id}`)
- [ ] Table-driven Extra tests (no official suite; write from the docs)

## Milestone 6 — Rendering

- [ ] Renderer walks AST → HTML
- [ ] Render parser HTML into `GtkTextBuffer` with Pango markup
- [ ] Syntax-aware styling for code blocks
- [ ] CSS-driven theme via GResource (`data/`)

## Milestone 7 — UX

- [ ] File open dialog + CLI arg
- [ ] Recent files
- [ ] Keyboard shortcuts (Ctrl+O, Ctrl+R, quit)
- [ ] Live reload on file change

## Design notes

- Keep `parser.c` free of GTK includes; test it headlessly.
- Add `MdOptions` and the AST `kind` enum **now** — retrofitting an AST is far
  harder than growing one.
- Return `GError *` from any fallible operation; never swallow errors.
- Favor `GString` over manual buffers while building HTML.
- Use CommonMark + GFM `spec.txt` as table-driven vectors; report % passing.