# Shiny Chainsaw

[![build](https://github.com/Fate1099/shiny-chainsaw/actions/workflows/ci.yml/badge.svg)](https://github.com/Fate1099/shiny-chainsaw/actions/workflows/ci.yml)

A **pure, lightweight Markdown viewer** written in **C** with **GTK3** and the
**Meson** build system. It has no runtime dependency on external Markdown
renderers — the parser lives in-repo and is implemented in pure C.

## Features

- Standalone GTK3 desktop app
- Pure-C Markdown parser (no external renderer, no GTK dependency in the core)
- Parser unit-testable headlessly
- Inline image rendering (remote via libsoup3, local files, relative paths)
- GFM tables with alignment, rendered as an aligned monospace block
- GFM strikethrough, task lists, and autolinks
- Nested lists indented by depth
- Raw HTML and HTML comments kept as literal text in the view (nothing is dropped)
- Live reload: the open file re-renders when it changes on disk, keeping the
  scroll position and the current search results
- Recent files in the menu, persisted to `<XDG config dir>/shiny-chainsaw/recent.txt`

## Requirements

- GCC
- Meson ≥ 0.63, Ninja
- GTK3 (≥ 3.24) dev headers
- GLib dev headers
- libsoup3 dev headers (for remote image loading)

## Build & run

```sh
meson setup build
ninja -C build
./build/src/shiny-chainsaw            # open a file picker
./build/src/shiny-chainsaw -f doc.md  # open a file directly
```

## Test

```sh
meson test -C build                        # parser, conformance, render, robustness
meson test -C build test_parser            # a single suite
meson compile -C build lint                # clang-format + clang-tidy
```

The `conformance` suite runs the vendored specs in `tests/spec/` through
`md_parse` + `md_render_html` and diffs the HTML line by line:
**652/652 CommonMark** and **677/677 GFM** examples pass. The `render` suite
drives `md_render_to_buffer` against a real `GtkTextView` and also drives the
`GtkApplication` for live reload and the recent-file list; both need a display
and print `skipped` (exit 0) when one is not available.

`lint` needs `clang-format` and/or `clang-tidy`; whichever is missing is
skipped rather than failing the build.

## Packages

**RPM** (Fedora-style; needs `rpmbuild` and the `%meson` RPM macros from
`redhat-rpm-config`):

```sh
./packaging/build-rpm.sh            # build RPMs into dist/rpmbuild/
./packaging/build-rpm.sh install    # build and sudo dnf install
```

The script validates the version matches `meson.build`, builds and tests a
release build, archives the source with `meson dist`, and feeds it to
`rpmbuild`. See `packaging/shiny-chainsaw.spec`.

**Debian** (debhelper ≥ 12 drives Meson directly, so there is no Makefile
shim; `debian/` holds the packaging):

```sh
dpkg-buildpackage -b -uc -us        # .deb beside the source tree
```

Tagged releases run `.github/workflows/release.yml`, which builds the source
tarball, the RPMs (Fedora container) and the `.deb`s (Debian 12 and 13
containers) and attaches them all to a GitHub Release.

## Project layout

- `src/`    — app source (entry point, GTK UI, pure-C parser, GTK renderer, shortcuts, recent-file list)
- `tests/`  — parser unit tests, the spec conformance harness, the render suite
- `data/`   — GLib resources (bundled manual), desktop file, GSettings schema
- `docs/`   — design notes and roadmap
- `scripts/`— lint target and the generators for the spec fixtures and entity table
- `packaging/` — RPM spec and the script that builds it
- `debian/`  — debhelper packaging for `.deb` builds

## Status

CommonMark block and inline parsing and the AST satisfy the vendored spec
suites (652/652 CommonMark, 677/677 GFM). Implemented: headings, paragraphs,
lists (including nesting), blockquotes, code, links and images, emphasis, GFM
tables (aligned by glyph width), strikethrough, task lists, autolinks, the GFM
raw-HTML grammar and its disallowed-tag filter, image rendering, file open,
search, zoom, keyboard shortcuts, live reload, and a persistent recent-files
list. Not implemented: Markdown Extra (the `MD_FEATURE_EXTRA_*` flags exist but
the parser ignores them). Release history is in [CHANGELOG.md](CHANGELOG.md);
details are in [docs/roadmap.md](docs/roadmap.md) and
[docs/design-notes.md](docs/design-notes.md).

## License

MIT. See [LICENSE](LICENSE).