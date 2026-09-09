#!/usr/bin/env sh
# Lint the C sources. Run via:  meson compile -C build lint
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SOURCES="$(find src tests -type f \( -name '*.c' -o -name '*.h' \) | sort)"
if [ -z "$SOURCES" ]; then
    echo "lint: no sources found"
    exit 1
fi

FAILED=0

# clang-format: report but do not rewrite (use clang-format -i to auto-fix).
if command -v clang-format >/dev/null 2>&1; then
    echo "lint: clang-format --dry-run --Werror"
    clang-format --dry-run --Werror $SOURCES || FAILED=1
else
    echo "lint: clang-format not installed (skipping format check)"
fi

# clang-tidy: static analysis (config in .clang-tidy). Run per file with the
# build's compile DB so the correct include paths/defines are used.
if command -v clang-tidy >/dev/null 2>&1; then
    echo "lint: clang-tidy"
    for f in $SOURCES; do
        clang-tidy "$f" -p build --quiet || FAILED=1
    done
else
    echo "lint: clang-tidy not installed (skipping)"
fi

if [ "$FAILED" -ne 0 ]; then
    echo "lint: FAILED"
    exit 1
fi
echo "lint: ok"