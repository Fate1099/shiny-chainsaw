# Roadmap

Tracked against the checklist in `AGENTS.md`.

## Milestone 1 — Project scaffolding (done)

- [x] Meson + Ninja build for GTK3
- [x] `MdApp` GTK application with a scrolling `GtkTextView`
- [x] Pure-C parser skeleton with escaping + `<p>` wrapper
- [x] Assert-based parser unit tests wired into `meson test`

## Milestone 2 — Block parsing

- [ ] Headings (`#`–`######`)
- [ ] Paragraphs and hard line breaks
- [ ] Unordered / ordered lists (incl. nesting)
- [ ] Fenced and indented code blocks
- [ ] Blockquotes
- [ ] Thematic breaks (`---`, `***`)
- [ ] HTML escaping and raw HTML passthrough policy

## Milestone 3 — Inline styling

- [ ] Bold / italic / strikethrough
- [ ] Inline code and code spans
- [ ] Links and images
- [ ] Autolinks (http, email)

## Milestone 4 — Rendering

- [ ] Render parser HTML into `GtkTextBuffer` with Pango markup
- [ ] Syntax-aware styling for code blocks
- [ ] CSS-driven theme via GResource (`data/`)

## Milestone 5 — UX

- [ ] File open dialog + CLI arg
- [ ] Recent files
- [ ] Keyboard shortcuts (Ctrl+O, Ctrl+R, quit)
- [ ] Live reload on file change

## Design notes

- Keep `parser.c` free of GTK includes; test it headlessly.
- Return `GError *` from any fallible operation; never swallow errors.
- Favor `GString` over manual buffers while building HTML.