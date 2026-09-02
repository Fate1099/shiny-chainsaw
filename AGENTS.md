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
./build/md-viewer  path/to/file.md
```

Reconfigure after adding source files:
```sh
meson setup --reconfigure build
```

## Testing

Run the test suite (CUnit or plain assert-based tests):

```sh
meson test -C build
```

Test sources live in `tests/` and are registered in `tests/meson.build`.
Keep parser tests table-driven: feed raw Markdown, assert on the emitted HTML.

## Project layout

- `src/`        — application source
  - `main.c`    — entry point (CLI arg parsing, launches app)
  - `app.c/.h`  — GTK application window, main view (GtkTextView / Pango)
  - `parser.c/.h` — pure Markdown → HTML/state parser (no GTK deps)
- `tests/`      — unit tests for the parser (pure C)
- `data/`       — GLib resources (CSS, app icon), bundled via GResource
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

- [x] Project scaffolding
- [ ] Core Markdown block parsing (headings, paragraphs, lists, code blocks)
- [ ] Inline styling (bold, italic, code, links)
- [ ] GTK rendering with syntax-aware Pango attributes
- [ ] File open/save, recent files, keyboard shortcuts
- [ ] Live reload on file change