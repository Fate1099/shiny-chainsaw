#!/usr/bin/env bash
# Build an RPM from the current checkout (Fedora/RPM style).
#
# Steps: release build + tests -> `meson dist` source archive -> rpmbuild.
# The spec Version must match meson.build; this is verified before building.
#
# Usage:
#   packaging/build-rpm.sh            build the RPMs
#   packaging/build-rpm.sh install    build, then sudo dnf install the RPM
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Keep the spec's Version in sync with meson.build.
VERSION="$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -1)"
SPEC_VERSION="$(sed -n 's/^Version:[[:space:]]*//p' packaging/md-viewer.spec | head -1)"
if [ "$VERSION" != "$SPEC_VERSION" ]; then
    echo "error: meson.build version '$VERSION' does not match spec Version '$SPEC_VERSION'" >&2
    exit 1
fi

if ! command -v rpmbuild >/dev/null 2>&1; then
    echo "error: rpmbuild not found; install it (e.g. 'sudo dnf install rpm-build')" >&2
    exit 1
fi

if ! git diff --quiet; then
    echo "warning: working tree has uncommitted changes; the source archive" >&2
    echo "         is built from HEAD and will not include them" >&2
fi

# Release build + tests, then a source archive for rpmbuild.
BUILD="build-rpm"
meson setup "$BUILD" --buildtype=release
meson compile -C "$BUILD"
meson test -C "$BUILD" --print-errorlogs
meson dist -C "$BUILD" --no-tests --allow-dirty

TARBALL=""
for d in "$BUILD/meson-dist" "meson-dist"; do
    if [ -f "$d/md-viewer-$VERSION.tar.xz" ]; then
        TARBALL="$d/md-viewer-$VERSION.tar.xz"
        break
    fi
done
if [ -z "$TARBALL" ]; then
    echo "error: meson dist did not produce md-viewer-$VERSION.tar.xz" >&2
    exit 1
fi

# rpmbuild tree.
TOPDIR="$ROOT/dist/rpmbuild"
rm -rf "$TOPDIR"
mkdir -p "$TOPDIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
cp "$TARBALL" "$TOPDIR/SOURCES/"
cp packaging/md-viewer.spec "$TOPDIR/SPECS/"

rpmbuild --define "_topdir $TOPDIR" -ba "$TOPDIR/SPECS/md-viewer.spec"

RPM="$(find "$TOPDIR/RPMS" -name 'md-viewer-[0-9]*.fc*.x86_64.rpm' | head -1)"
echo
echo "Built: $RPM"

if [ "${1:-}" = "install" ]; then
    sudo dnf install -y "$RPM"
fi