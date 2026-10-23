# AGENTS.md

Guidance for AI agents and contributors working in this repository.

## Project

`shiny-chainsaw` is a lightweight Markdown viewer in **C** with **GTK3** and Meson.
The Markdown parser is implemented in-repo (no external renderer). CI is GitHub
Actions (`.github/workflows/ci.yml`): configure, build, every test suite under
`xvfb`, lint, and valgrind on the headless suites.

## Build & run

```sh
meson setup build
ninja -C build
./build/src/shiny-chainsaw -f path/to/file.md   # or no arg -> file picker
```

Reconfigure after adding sources: `meson setup --reconfigure build`.
Deps beyond GTK3: `libsoup-3.0` (remote image loading) — see `src/meson.build`.

## Testing — read this before running it

```sh
meson test -C build                        # all suites, currently green
meson test -C build test_parser            # single test
```

The `conformance` suite (`commonmark-spec`, `gfm-spec`) passes in full
(`652/652` CommonMark, `677/677` GFM). Conformance is measured by feeding the
vendored spec files in `tests/spec/` through `md_parse` + `md_render_html` and
diffing HTML (line-accurate, so blank-line and indentation details count);
report pass percentage when working on the parser.

In `tests/spec/gfm.txt` each example fence carries the extension flag from the
spec (`example autolink`, `example table`, `example strikethrough`,
`example tasklist`, `example tagfilter`); the harness enables only those
features for that example. GFM core (the raw-HTML grammar) is always on in
`gfm` mode. Regenerate the file with `python3 scripts/gen-gfm-spec.py`.

`tests/spec/commonmark.txt` is CommonMark 0.31.2 `spec.txt` byte for byte — the
spec source already uses the 32-backtick fences the harness reads. Regenerate it
with `python3 scripts/gen-commonmark-spec.py` (optionally `--version 0.31.2`)
rather than editing it by hand.

Parser tests are plain asserts, table-driven: feed raw Markdown, assert on emitted
HTML. Every test binary compiles `src/parser.c` directly — hence the pure-core rule
below.

The `robustness` suite (`test_robustness`) feeds truncated, deeply nested, and
invalid-UTF-8 input through the parser and asserts it neither crashes nor leaks.
Bounds-related regressions (a line shorter than the container indent, a task
marker cut off at end of line) belong there.

The `render` suite (`test_render`) drives `md_render_to_buffer` against a
`GtkTextView` and asserts on the buffer text. It needs a display connection and
prints `skipped` plus exit 0 when `gtk_init_check()` fails, so headless runs do
not fail.

The `unit` suite also covers `src/recent.c` (`test_recent`): ordering,
deduplication, truncation at `MD_RECENT_MAX`, and the round trip through the
list file. `src/recent.c` is GLib only, so it runs without a display.

`test_live_reload` (suite `render`) drives the real `GtkApplication`: it
registers under a test-only application id, activates, opens a file, rewrites
it, and asserts that the monitor re-renders, that the recent list is written,
and that the `open-recent` action and `md_app_reload` work. It points
`XDG_CONFIG_HOME` at a temporary directory so it never touches the developer's
recent-file list, and skips itself without a display.

## Lint

```sh
meson compile -C build lint
```

- `clang-format` runs `--dry-run --Werror` only; it never auto-fixes. Fix with
  `clang-format -i <files>`.
- `clang-tidy` uses the ninja compile database at `-p build`, so `build/` must
  already exist (run `meson setup build` first).
- Missing tools are skipped, not fatal.
- Style is not LLVM-default: `.clang-format` sets `BreakBeforeBraces: Allman`,
  `IndentWidth: 4`, `ColumnLimit: 100`, `PointerAlignment: Left`.

## Architecture

- `src/main.c` — translates `-f`/`--file` into a positional argument so
  GApplication's `open` signal handles it (works both for a fresh launch and for
  a handoff to an already-running instance).
- `src/app.c` — `GtkApplication` (`io.github.Fate1099.shiny-chainsaw`, `G_APPLICATION_HANDLES_OPEN`),
  window, read-only `GtkTextView`, sidebar, search, zoom.
- `src/parser.c` — pure Markdown → AST (`MdNode`) plus `md_render_html()`; GLib only.
- `src/render.c` — AST → `GtkTextBuffer` with Pango markup, image loading via libsoup.
- `src/recent.c` — the recently opened files: read/add/write of one path per line
  in `<XDG config dir>/shiny-chainsaw/recent.txt`, most recent first, capped at
  `MD_RECENT_MAX`. GLib only, so it is testable headlessly.
- `src/shortcuts.c` — one table (`SHORTCUTS[]`) registers every app action and accel;
  add shortcuts there, not ad hoc. Each row can carry an argument type (e.g.
  `open-recent` takes a path), so menu items can pass a target to their action.

**Pure core**: `parser.c`/`parser.h` must never include GTK headers. Parsing is
GLib-only; GTK touches only `app.c`/`render.c`. Breaking this breaks the headless
tests.

**One parser, feature flags**: CommonMark is the core; GFM and Markdown Extra are
`MdFeature` bit flags in `MdOptions` (e.g. `MD_FEATURE_GFM_TABLES`), not separate
parsers. `app.c` currently enables the four GFM extension flags plus the GFM raw
HTML grammar; Extra flags exist in the
header but are unimplemented.

## Environment quirks

- The GSettings schema (`data/io.github.Fate1099.shiny-chainsaw.gschema.xml`) is only usable after
  install. Run from `build/` without installing and `image-max-width` silently falls
  back to `0` (auto-fit images) rather than erroring.
- `build/`, `build-rpm/`, `dist/`, `meson-dist/` are generated and gitignored.

## Packaging

### RPM

```sh
./packaging/build-rpm.sh            # RPMs into dist/rpmbuild/
./packaging/build-rpm.sh install    # build + sudo dnf install
```

Requires `rpmbuild` and the `%meson` macros (`redhat-rpm-config`). The script fails
if `Version:` in `packaging/shiny-chainsaw.spec` does not match `version:` in
`meson.build` — bump both together. It builds a separate release build in
`build-rpm/` and runs every suite, conformance included; the render and
live-reload suites skip themselves in the headless build environment.

### Debian

```sh
dpkg-buildpackage -b -uc -us       # .deb beside the source tree
```

`debian/rules` uses debhelper's Meson buildsystem (debhelper ≥ 12, so Debian 12
and 13 both work). `debian/changelog` carries the release version and date and
must be bumped together with `meson.build` and the RPM spec.

### Releases

Pushing a `v*` tag runs `.github/workflows/release.yml`: it builds the source
tarball with `meson dist`, the RPMs in a `fedora:41` container, and `.deb`s in
`debian:12` and `debian:13` containers, then attaches everything to a GitHub
Release. `ci.yml` only runs on pushes to `main` and pull requests.

## Conventions

- **Ownership**: GLib conventions; functions returning new references use `_new` and
  are documented `(transfer full)`.
- **Errors**: propagate via `GError **` out-params; never swallow.
- **Naming**: `md_scope_verb` (e.g. `md_parse`, `md_app_open_file`).
- **Comments**: only *why*, not *what*.
- **Memory**: pair every `g_` allocation with `g_free`/`g_object_unref`; valgrind
  changes that touch memory before merging.
- **Security**: escape user content (`g_markup_escape_text`) before it reaches Pango
  markup; no fixed-size buffers.

## Status

CommonMark block/inline parsing and the AST satisfy the vendored spec suites
(652/652 CommonMark, 677/677 GFM). Implemented: headings, paragraphs, lists
(incl. nesting), blockquotes, code, links/images, emphasis, GFM tables
(glyph-width aligned), strikethrough, task lists, autolinks (bare `www.`,
`http://`, `ftp://`, e-mail, `mailto:`/`xmpp:`), GFM raw-HTML grammar and the
disallowed-raw-HTML filter, HTML comments (passed through as raw HTML), image
rendering, file open/search/zoom/shortcuts, live reload of the open file, and a
recent-files list persisted in the user config dir. Not implemented: Markdown
Extra.
Details: `docs/roadmap.md`, `docs/design-notes.md`, `CHANGELOG.md`.
