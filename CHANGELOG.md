# Changelog

All notable changes to md-viewer are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Images and text no longer overlap after images finish loading asynchronously.
  GtkTextView caches per-line displays, so after an anchored image grew we now
  drop the whole cached layout (re-setting the wrap mode) and repaint, instead
  of only re-queueing a resize that left text and later images drawing over
  the image. Images that load before the window is first laid out are capped
  instead of being embedded at full size.

### Added

- GFM tables (behind `MD_FEATURE_GFM_TABLES`): header/delimiter rows with
  left/center/right alignment, inline formatting in cells, and rows with
  missing cells padded. Rendered as an aligned monospace block with pipes and
  dashes.
- Nested list items are indented by depth in the rendered view.
- Inline image rendering: images are embedded in the document and loaded
  asynchronously (remote via libsoup3, local files, and paths relative to the
  opened document). The alt text is shown as a caption under the image; a
  broken-image placeholder is shown when loading or decoding fails.
- `image-max-width` GSettings key (`org.example.mdviewer`) to cap image width;
  0 (default) fits images to the window width without upscaling.
- HTML comments (`<!-- ... -->`) are stripped from the output.
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
- Zoom controls (Ctrl+= / Ctrl+- / Ctrl+0).
- Central keyboard-shortcut table (Ctrl+O, Ctrl+F, F9, quit, about).
- Bundled manual rendered via GResource on first launch.
- Meson build with lint target (clang-format / clang-tidy) and table-driven
  parser test suite (46 cases).
- RPM packaging pipeline (`packaging/md-viewer.spec` + `build-rpm.sh`) that
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

- GFM extensions (tables, task lists, strikethrough, autolinks)
- Markdown Extra extensions (definition lists, footnotes, abbreviations)
- CommonMark `spec.txt` conformance harness
- Live reload on file change