# AGENTS.md

Guidance for AI agents and contributors working in this repository.

## Project

`md-viewer` is a **pure, lightweight Markdown viewer** written in **C** using
**GTK3** and the Meson build system. It has no runtime dependency on external
Markdown renderers; the parser is implemented in-repo in C.

## Environment

- Compiler: GCC
- Build system: Meson + Ninja
- GTK: **GTK3** (`gtk+-3.0`, verified `3.24.52`)
- Language standard: C11

## Build & run

```sh
meson setup build
ninja -C build
./build/src/md-viewer -f path/to/file.md
```

Reconfigure after adding source files:
```sh
meson setup --reconfigure build
```

## Lint

Formatting and static analysis live behind a single target. It requires
`clang-format` and/or `clang-tidy` to be installed; each missing tool is
skipped with a notice rather than failing the build.

```sh
meson compile -C build lint
```

Config: `.clang-format` (formatting) and `.clang-tidy` (static analysis checks).
Script: `scripts/lint.sh`.
Fix formatting with `clang-format -i <files>`.

## Testing

Run the test suite (plain assert-based, no framework):

```sh
meson test -C build
```

Test sources live in `tests/` and are registered in `tests/meson.build`.
Each test binary compiles `src/parser.c` directly, which is why the parser must
stay free of GTK includes. Keep parser tests table-driven: feed raw Markdown,
assert on the emitted HTML.

## Project layout

- `src/`        — application source
  - `main.c`      — entry point (CLI arg parsing, launches app)
  - `app.c/.h`    — GTK application window, main view (GtkTextView / Pango)
  - `parser.c/.h` — pure Markdown → HTML/state parser (no GTK deps)
  - `render.c/.h` — GTK-side renderer: AST → GtkTextBuffer with syntax-aware Pango tags
  - `shortcuts.c/.h` — central table for all keyboard shortcuts / app actions
- `tests/`      — unit tests for the parser (pure C)
- `data/`       — GLib resources (bundled manual), via GResource
- `docs/`       — design notes, roadmap, usage docs
- `meson.build` — top-level build definition

## Conventions

- **Pure core**: `parser.c` must not include GTK headers. Keep rendering (GTK)
  separate from parsing (pure C). This keeps parser unit-testable without a display.
- **Ownership**: follow GLib conventions — functions returning new references end
  with `_new` and are documented `(transfer full)`; out-params documented.
- **Memory**: always pair `g_` allocations with matching `g_free`/`g_object_unref`.
  Run the project under valgrind before merging changes that touch memory.
- **Naming**: functions use `project_name_scope_verb` (e.g. `md_parser_parse`).
- **Errors**: propagate via `GError *` out-params; do not silently swallow.
- **No comments unless they explain *why***, not *what*. Keep it minimal.
- **Formatting**: follow existing style; run `clang-format` if a config is present.
- **Security**: never trust file contents blindly — guard against malformed input,
  unbounded buffers, and escaping of HTML in rendered output.

## Roadmap highlights

The parser targets a **single superset spec with feature flags** (not three
separate parsers): a **CommonMark** core with **GFM** and **Markdown Extra**
as layered extensions. One AST + one renderer serves all three. Full details
in `docs/roadmap.md`.

- [x] Project scaffolding
- [ ] CommonMark block + inline parsing (headings, paragraphs, lists, code)
- [ ] GFM extensions (tables, task lists, strikethrough, autolinks)
- [ ] Markdown Extra extensions (definition lists, footnotes, abbreviations)
- [ ] AST (`MdOptions`, `MdNode kind`) + `spec.txt` conformance harness
- [ ] GTK rendering with syntax-aware Pango attributes
- [ ] File open/save, recent files, keyboard shortcuts
- [ ] Live reload on file change