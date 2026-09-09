# md-viewer

A **pure, lightweight Markdown viewer** written in **C** with **GTK3** and the
**Meson** build system. It has no runtime dependency on external Markdown
renderers — the parser lives in-repo and is implemented in pure C.

## Features

- Standalone GTK3 desktop app
- Pure-C Markdown parser (no external renderer, no GTK dependency in the core)
- Parser unit-testable headlessly

## Requirements

- GCC
- Meson ≥ 0.63, Ninja
- GTK3 (≥ 3.24) dev headers
- GLib dev headers

## Build & run

```sh
meson setup build
ninja -C build
./build/src/md-viewer            # open a file picker
./build/src/md-viewer -f doc.md  # open a file directly
```

## Test

```sh
meson test -C build
```

## Project layout

- `src/`    — app source (entry point, GTK UI, pure-C parser, GTK renderer, shortcuts)
- `tests/`  — parser unit tests
- `data/`   — GLib resources (bundled manual)
- `docs/`   — design notes and roadmap

## Status

CommonMark block + inline parsing (headings, paragraphs, lists, code, blockquotes,
links/images, emphasis) with a GTK renderer and file-open/search UI. GFM and
Markdown Extra extensions are planned but not yet implemented; see
[AGENTS.md](AGENTS.md) and [docs/roadmap.md](docs/roadmap.md).

## License

MIT. See [LICENSE](LICENSE).