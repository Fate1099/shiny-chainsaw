# AGENTS.md

Guidance for AI agents and contributors working in this repository.

## Project

`md-viewer` is a lightweight Markdown viewer in **C** with **GTK3** and Meson.
The Markdown parser is implemented in-repo (no external renderer). CI is GitHub
Actions (`.github/workflows/ci.yml`): configure, build, every test suite under
`xvfb`, lint, and valgrind on the headless suites.

## Build & run

```sh
meson setup build
ninja -C build
./build/src/md-viewer -f path/to/file.md   # or no arg -> file picker
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
- `src/app.c` — `GtkApplication` (`org.example.mdviewer`, `G_APPLICATION_HANDLES_OPEN`),
  window, read-only `GtkTextView`, sidebar, search, zoom.
- `src/parser.c` — pure Markdown → AST (`MdNode`) plus `md_render_html()`; GLib only.
- `src/render.c` — AST → `GtkTextBuffer` with Pango markup, image loading via libsoup.
- `src/shortcuts.c` — one table (`SHORTCUTS[]`) registers every app action and accel;
  add shortcuts there, not ad hoc.

**Pure core**: `parser.c`/`parser.h` must never include GTK headers. Parsing is
GLib-only; GTK touches only `app.c`/`render.c`. Breaking this breaks the headless
tests.

**One parser, feature flags**: CommonMark is the core; GFM and Markdown Extra are
`MdFeature` bit flags in `MdOptions` (e.g. `MD_FEATURE_GFM_TABLES`), not separate
parsers. `app.c` currently enables the four GFM extension flags plus the GFM raw
HTML grammar; Extra flags exist in the
header but are unimplemented.

## Environment quirks

- The GSettings schema (`data/org.example.mdviewer.gschema.xml`) is only usable after
  install. Run from `build/` without installing and `image-max-width` silently falls
  back to `0` (auto-fit images) rather than erroring.
- `build/`, `build-rpm/`, `dist/`, `meson-dist/` are generated and gitignored.

## RPM packaging

```sh
./packaging/build-rpm.sh            # RPMs into dist/rpmbuild/
./packaging/build-rpm.sh install    # build + sudo dnf install
```

Requires `rpmbuild` and the `%meson` macros (`redhat-rpm-config`). The script fails
if `Version:` in `packaging/md-viewer.spec` does not match `version:` in
`meson.build` — bump both together. It builds a separate release build in
`build-rpm/` and runs every suite, conformance included; the render suite skips
itself in the headless build environment.

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
rendering, file open/search/zoom/shortcuts. Not implemented: Markdown Extra,
live reload.
Details: `docs/roadmap.md`, `docs/design-notes.md`, `CHANGELOG.md`.
