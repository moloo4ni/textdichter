#!/bin/sh
# Builds the release tarball in dist/: the editor linked against the system
# Qt, so it follows the desktop theme, and with cmark built in, since the name
# of cmark's library changes with every release. Run from the source tree.
set -eu

version=$(sed -n 's/^project(textdichter VERSION \([^ )]*\).*/\1/p' CMakeLists.txt)
name=textdichter-$version-linux-$(uname -m)
cmark=0.31.2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL "https://github.com/commonmark/cmark/archive/refs/tags/$cmark.tar.gz" | tar -xz -C "$work"
cmake -S "$work/cmark-$cmark" -B "$work/cmark-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$work/cmark" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$work/cmark-build"
cmake --install "$work/cmark-build"

# Qt built with -mno-direct-extern-access, as on Arch, refuses programs built
# without it; a Qt built without it takes them either way.
PKG_CONFIG_PATH="$work/cmark/lib/pkgconfig" cmake -S . -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DCMAKE_CXX_FLAGS=-mno-direct-extern-access
cmake --build "$work/build"
ctest --test-dir "$work/build" --output-on-failure
cmake --install "$work/build" --prefix "$work/$name" --strip
mkdir -p "$work/$name/share/doc/textdichter"
cp LICENSE README.md "$work/$name/share/doc/textdichter"

# The binary must start, and with the translations in it.
LC_ALL=ru_RU.UTF-8 QT_QPA_PLATFORM=offscreen "$work/$name/bin/textdichter" --help | grep -q 'Лёгкий редактор'

mkdir -p dist
tar -C "$work" --owner=0 --group=0 -czf "dist/$name.tar.gz" "$name"
(cd dist && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
