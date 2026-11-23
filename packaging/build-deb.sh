#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build a binary .deb from a Meson dist archive of Stellar WiM.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${BUILD_DIR:-$root/build-stellar}
changelog=$root/debian/changelog

if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
	echo "dpkg-buildpackage is required" >&2
	exit 1
fi
if ! command -v dh >/dev/null 2>&1; then
	echo "debhelper (dh) is required" >&2
	exit 1
fi

full_version=$(dpkg-parsechangelog -l "$changelog" -S Version)
version=${full_version%%-*}
archive=stellar-wim-$version.tar.xz

# Include subprojects so the archive builds offline; debhelper passes
# --wrap-mode=nodownload to Meson by default.
meson dist -C "$build_dir" --allow-dirty --include-subprojects

dist_dir=$(cd "$build_dir/meson-dist" && pwd)
if [ ! -f "$dist_dir/$archive" ]; then
	echo "expected archive $dist_dir/$archive was not created" >&2
	exit 1
fi

# dpkg tools do not like spaces in paths, and this checkout has them.
work=$(mktemp -d /tmp/stellar-wim-deb.XXXXXX)
cleanup() {
	chmod -R u+rwx "$work" 2>/dev/null || true
	rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
# The IPC test drops privileges to check SO_PEERCRED; it needs to traverse
# this directory, and mktemp creates it with mode 0700.
chmod 755 "$work"

cp "$dist_dir/$archive" "$work/stellar-wim_$version.orig.tar.xz"
mkdir "$work/src"
tar -xf "$dist_dir/$archive" -C "$work/src"
chmod -R a+rX "$work/src"
source_dir=$(find "$work/src" -mindepth 1 -maxdepth 1 -type d | head -1)
if [ -z "$source_dir" ]; then
	echo "the Meson archive did not contain a source directory" >&2
	exit 1
fi
if [ ! -f "$source_dir/debian/changelog" ]; then
	echo "the Meson archive does not contain debian/changelog" >&2
	exit 1
fi

(
	cd "$source_dir"
	dpkg-buildpackage -us -uc -b
)

# dpkg-buildpackage writes into the parent of the source directory, i.e.
# $work/src, not directly into $work.
deb=$(find "$work" -name 'stellar-wim_*.deb' | head -1)
if [ -z "$deb" ]; then
	echo "dpkg-buildpackage did not produce a stellar-wim .deb" >&2
	exit 1
fi
target=$root/$(basename "$deb")
cp "$deb" "$target"
echo "Built: $target"
