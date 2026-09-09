# Changelog

All notable changes to md-viewer are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

### Planned

- GFM extensions (tables, task lists, strikethrough, autolinks)
- Markdown Extra extensions (definition lists, footnotes, abbreviations)
- CommonMark `spec.txt` conformance harness
- Live reload on file change