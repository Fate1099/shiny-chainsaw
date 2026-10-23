# Changelog

All notable changes to shiny-chainsaw are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- The RPM build no longer skips the conformance suite. `packaging/build-rpm.sh`
  and the spec's `%check` run every suite, since both spec suites now pass.
- The vendored `tests/spec/gfm.txt` fixture was mangled (nested emphasis
  examples collapsed). It is now regenerated verbatim from the official spec
  page by `scripts/gen-gfm-spec.py`, and the harness honours the per-example
  extension flags (`example autolink`, `example table`, …) so extension
  features are only enabled for the examples that declare them.
- GFM raw HTML: comments now follow the GFM grammar (`<!--` … `-->`, no `-->`
  inside, text may not start with `>` or `->` or end with `-`), and the
  disallowed-raw-HTML filter renders `<script>`, `<style>`, `<textarea>`,
  `<title>`, `<xmp>`, `<iframe>`, `<noembed>`, `<noframes>` and `<plaintext>`
  as escaped text in inline content and in HTML block types 2 and 6
  (`MD_FEATURE_GFM_RAW_HTML`).
- Pipe characters escaped as `\|` inside a table cell are no longer treated as
  a cell boundary.
- Table body rows that continue a paragraph without a pipe are now absorbed as
  lazy continuation rows instead of terminating the table.
- `xmpp:` autolinks keep one path segment (`xmpp:foo@bar.baz/txt`), while
  `mailto:` addresses stop before a `/`, as in the spec.
- The GTK renderer silently dropped raw HTML: `MD_NODE_HTML_BLOCK` and
  `MD_NODE_RAW_HTML` nodes are now inserted as literal text (monospace tag),
  so `<div>` blocks and inline tags no longer vanish from the view.
- GFM table column alignments are stored dynamically, so tables wider than 16
  columns keep their per-column alignment instead of being capped at 16.
- Link-reference destinations are URI-escaped the same way as inline link
  destinations, so the AST holds one consistent form.
- `is_ascii_punctuation` matched the NUL terminator of its own table, so a
  backslash at the end of a link destination counted as an escape and read one
  character past the end of the string. The function now matches the CommonMark
  punctuation set by ranges, and the duplicate `is_ascii_punct` helper is gone.
- `parse_task_marker` no longer reads past the end of a line such as `- [`.
- A lone `:` in a GFM delimiter row (`|:|`) is no longer accepted as an
  alignment cell; the previous check underflowed and let it through.
- Lazy continuation lines and HTML-block lines shorter than the container
  indent were read past their terminator (`lines[k] + indent`). A
  `strip_indent` helper now clamps the offset.
- Memory: `normalize_ref_key` leaked its `GString`, the `[` / `![` token node
  consumed by a link was never freed, and the zoom CSS provider was a process
  static. Image loads now hold their own references to the view and the
  in-flight-load list, and `md_app_dispose` cancels pending loads before the
  soup session is destroyed.
- Image scaling computed `(h * nw) / w` in `gint`, which overflowed for large
  images; the multiplication is now `gint64`.
- `md_parse` reports a `GError` (`MD_PARSER_ERROR_DOMAIN`) for NULL input
  instead of aborting through `g_return_val_if_fail`.
- Zoom in is registered for both `Ctrl+=` and `Ctrl++`; the tooltip and the
  bundled manual now say so.
- `-f`/`--file` no longer discards the remaining command-line arguments.
- Dead code removed: the `example disabled` branch in the spec harness (no
  fixture uses it), the `tagfilter` flag mapping (`tagfilter` is part of the
  GFM raw-HTML grammar, which gfm mode always enables), the unused `tag`
  out-parameter of `html_block_type` / `html_block_ends`, the always-true
  `is_lazy` expression, and a `list->children == NULL` test that could never
  be true.

- Table columns are padded by glyph width instead of byte length, so cells
  with non-ASCII characters (e.g. "für") no longer misalign the pipes and
  dashes in the rendered monospace block. Cell width now also accounts for
  inline formatting (bold, links, code) and wide (CJK) characters.
- Images and text no longer overlap after images finish loading asynchronously.
  GtkTextView caches per-line displays, so after an anchored image grew we now
  drop the whole cached layout (re-setting the wrap mode) and repaint, instead
  of only re-queueing a resize that left text and later images drawing over
  the image. Images that load before the window is first laid out are capped
  instead of being embedded at full size.

### Added

- Live reload: the open file is watched with a `GFileMonitor` and re-rendered
  when it changes on disk. Saves that rewrite identical content are ignored, a
  burst of events is collapsed into one reload (200 ms debounce), the scroll
  position is restored, and the current search query is re-run. "Reload file"
  in the menu, `Ctrl+R`.
- Recent files: `src/recent.c` keeps the ten most recently opened paths, one per
  line, in `<XDG config dir>/shiny-chainsaw/recent.txt`. The header-bar menu has an
  "Open recent" submenu; opening a file moves it to the front of the list.
- `tests/test_recent.c` (suite `unit`) exercises the recent list: ordering,
  deduplication, truncation, and the round trip through the file.
- `tests/test_live_reload.c` (suite `render`) drives the real `GtkApplication`
  and asserts that the monitor reloads, that the recent list is written, and
  that `open-recent` and the reload action work. It needs a display and skips
  itself without one.
- The shortcut table in `src/shortcuts.c` now carries an argument type, so an
  action can take a value (`open-recent` takes the path of the file to open).
- `scripts/gen-commonmark-spec.py`, which regenerates `tests/spec/commonmark.txt`
  from the pinned CommonMark 0.31.2 spec source instead of relying on a hand
  copied file. The fixture is byte identical to the upstream `spec.txt`.
- `tests/test_robustness.c`, a robustness suite (suite `robustness`) that runs
  truncated constructs, 100k-character lines, 2000-level nesting, 400-column
  tables, and invalid UTF-8 through the parser under valgrind.

- `tests/test_render.c`, a render smoke test (suite `render`) that checks the
  buffer text the app actually shows: headings with their tag, raw HTML blocks
  and inline tags, aligned tables, task-list glyphs, code, blockquotes, and
  that re-rendering clears the buffer. It skips cleanly without a display.
- GFM strikethrough (`~x~` / `~~x~~`, behind `MD_FEATURE_GFM_STRIKETHROUGH`):
  one or two tildes wrap `<del>…</del>`; three or more stay literal. Rendered
  with a struck-through Pango tag.
- GFM task lists (behind `MD_FEATURE_GFM_TASKLISTS`): `- [ ]` / `- [x]` /
  `- [X]` items emit a semantic `<input type="checkbox">` (checked/disabled,
  matching the GFM spec) in HTML and `☐`/`☑` glyphs in the GTK view.
- GFM autolinks (behind `MD_FEATURE_GFM_AUTOLINKS`): bare `www.` and
  `http(s)://` URLs, email addresses, and `mailto:`/`xmpp:` protocols, with
  GFM's domain, trailing-punctuation, parenthesis-balancing, and entity rules.
  Links in the GTK view reuse the existing link style.
- Tight list items now separate their text from a following nested list with a
  newline in the HTML output, matching cmark.
- GFM tables (behind `MD_FEATURE_GFM_TABLES`): header/delimiter rows with
  left/center/right alignment, inline formatting in cells, and rows with
  missing cells padded. Rendered as an aligned monospace block with pipes and
  dashes.
- Nested list items are indented by depth in the rendered view.
- Inline image rendering: images are embedded in the document and loaded
  asynchronously (remote via libsoup3, local files, and paths relative to the
  opened document). The alt text is shown as a caption under the image; a
  broken-image placeholder is shown when loading or decoding fails.
- `image-max-width` GSettings key (`io.github.Fate1099.shiny-chainsaw`) to cap image width;
  0 (default) fits images to the window width without upscaling.
- HTML comments (`<!-- ... -->`) are recognized as raw HTML by the GFM grammar
  and passed through to the output.
- Sidebar outline now has a sane minimum width (no longer locked to the widest
  heading) and defaults to ~12% of the window width.
- Pure-C CommonMark parser producing an `MdNode` AST: ATX headings,
  paragraphs, unordered/ordered lists (with nesting), fenced and indented
  code blocks, blockquotes, thematic breaks, links, images, emphasis/strong,
  code spans, and backslash escapes.
- Feature-flag scaffold (`MdOptions`/`MdFeature`) for future GFM and
  Markdown Extra extensions.
- HTML renderer for headless testing.
- GTK3 application with syntax-aware Pango rendering (headings, bold/italic,
  code, links, blockquotes, list items).
- File open via dialog and `-f/--file` CLI argument; window title and
  statusbar reflect the current file.
- Live buffer search with match count and prev/next navigation.
- Sidebar outline generated from parsed headings (click to jump).
- Zoom controls (Ctrl+= / Ctrl++ / Ctrl+- / Ctrl+0).
- Central keyboard-shortcut table (Ctrl+O, Ctrl+F, F9, quit, about).
- Bundled manual rendered via GResource on first launch.
- Meson build with lint target (clang-format / clang-tidy) and table-driven
  parser test suite (79 cases).
- RPM packaging pipeline (`packaging/shiny-chainsaw.spec` + `build-rpm.sh`) that
  builds a release, runs the tests, and produces/installs an RPM.

### Fixed

- Trailing carriage returns from CRLF input no longer leak into output or
  break blank-line detection.
- AST free now releases all child nodes (previously leaked the entire tree).
- `-f/--file` previously conflicted with GApplication option parsing; now
  registered via GApplication and handled correctly.
- Inline link parsing no longer drops a following parenthesized run or leaks
  the link target.
- Code spans strip one leading/trailing space correctly (including
  all-space spans).
- Indented and nested list markers parse as nested lists instead of literal
  paragraph text.
- Pango tags are named and reused across renders, so re-opening files no
  longer grows the buffer's tag table.
- Removed stale-iterator use in the GTK renderer (offsets used instead).
- Launching `-f` at an already-running instance now opens the file in the
  existing window instead of silently dropping it.
- Opening a file via `-f`, `--file`, or a positional argument now works on a
  fresh launch (previously GApplication swallowed the option and the window
  was never built for positional args).
- RPM package now includes desktop integration: a `.desktop` file registering
  `text/markdown` MIME handling, an SVG icon, and
  `update-desktop-database`/`gtk-update-icon-cache` post scripts.

### Planned

- Markdown Extra extensions (definition lists, footnotes, abbreviations)
- A CSS theme file in `data/`
- Tagged releases, with `CHANGELOG.md` versioned and `meson.build` /
  `packaging/shiny-chainsaw.spec` kept in step