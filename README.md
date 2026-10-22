# md-viewer

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
./build/src/md-viewer            # open a file picker
./build/src/md-viewer -f doc.md  # open a file directly
```

## Test

```sh
meson test -C build                        # parser, conformance, render suites
meson test -C build test_parser            # a single suite
meson compile -C build lint                # clang-format + clang-tidy
```

The `conformance` suite runs the vendored specs in `tests/spec/` through
`md_parse` + `md_render_html` and diffs the HTML line by line:
**652/652 CommonMark** and **677/677 GFM** examples pass. The `render` suite
drives `md_render_to_buffer` against a real `GtkTextView`; it needs a display
and prints `skipped` (exit 0) when one is not available.

`lint` needs `clang-format` and/or `clang-tidy`; whichever is missing is
skipped rather than failing the build.

## Package (RPM)

Build and install an RPM for Fedora-style distros (requires `rpmbuild`, the
`meson` and `ninja-build` packages, and the `%meson` RPM macros from
`redhat-rpm-config`):

```sh
./packaging/build-rpm.sh            # build RPMs into dist/rpmbuild/
./packaging/build-rpm.sh install    # build and sudo dnf install
```

The script validates the version matches `meson.build`, builds + tests a
release build, archives the source with `meson dist`, and feeds it to
`rpmbuild`. See `packaging/md-viewer.spec`.

## Project layout

- `src/`    — app source (entry point, GTK UI, pure-C parser, GTK renderer, shortcuts)
- `tests/`  — parser unit tests, the spec conformance harness, the render suite
- `data/`   — GLib resources (bundled manual), desktop file, GSettings schema
- `docs/`   — design notes and roadmap
- `scripts/`— lint target and the generators for the spec fixtures and entity table

## Status

CommonMark block and inline parsing and the AST satisfy the vendored spec
suites (652/652 CommonMark, 677/677 GFM). Implemented: headings, paragraphs,
lists (including nesting), blockquotes, code, links and images, emphasis, GFM
tables (aligned by glyph width), strikethrough, task lists, autolinks, the GFM
raw-HTML grammar and its disallowed-tag filter, image rendering, file open,
search, zoom, and keyboard shortcuts. Not implemented: Markdown Extra (the
`MD_FEATURE_EXTRA_*` flags exist but the parser ignores them) and live reload
on file change. Release history is in [CHANGELOG.md](CHANGELOG.md); details are
in [docs/roadmap.md](docs/roadmap.md) and [docs/design-notes.md](docs/design-notes.md).

## License

MIT. See [LICENSE](LICENSE).