#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Verify that a staged install provides the Stellar WiM identity and does not
# install any Labwc-named binary, session, portal config or man page.
set -eu

build_dir=$1
case "$build_dir" in
	/*) build_dir_abs=$build_dir ;;
	*) build_dir_abs=$PWD/$build_dir ;;
esac
build_dir=$build_dir_abs

if ! command -v meson >/dev/null 2>&1; then
	echo "meson not found; skipping staged install test" >&2
	exit 0
fi

tmp=$(mktemp -d /tmp/stellar-packaging.XXXXXX)
cleanup() {
	chmod 700 "$tmp/dest" 2>/dev/null || true
	rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$tmp/dest"
(cd "$build_dir" && meson install --destdir "$tmp/dest") >"$tmp/install.log" 2>&1 || {
	echo "meson install failed" >&2
	tail -30 "$tmp/install.log" >&2
	exit 1
}

binary=$(find "$tmp/dest" -type f -path '*/bin/stellar-wim' | head -1)
if [ -z "$binary" ]; then
	echo "stellar-wim was not installed" >&2
	exit 1
fi
root=$(dirname "$(dirname "$binary")")

assert_file() {
	path=$1
	if [ ! -f "$path" ]; then
		echo "missing installed file: $path" >&2
		exit 1
	fi
}

assert_file "$root/bin/stellar-wim"
assert_file "$root/bin/stellar-wimctl"
assert_file "$root/share/wayland-sessions/stellar-wim.desktop"
assert_file "$root/share/xdg-desktop-portal/stellar-wim-portals.conf"
assert_file "$root/share/man/man1/stellar-wim.1"
assert_file "$root/share/man/man1/stellar-wimctl.1"
assert_file "$root/share/man/man5/stellar-wim-config.5"
assert_file "$root/share/man/man7/stellar-wim-ipc.7"
assert_file "$root/share/man/man7/stellar-wim-session.7"
assert_file "$root/share/icons/hicolor/scalable/apps/stellar-wim.svg"
assert_file "$root/share/doc/stellar-wim/rc.xml"
assert_file "$root/share/doc/stellar-wim/VERIFICATION.md"

desktop="$root/share/wayland-sessions/stellar-wim.desktop"
grep -q '^Type=Application$' "$desktop"
grep -q '^Name=Stellar WiM$' "$desktop"
grep -q '^Exec=stellar-wim$' "$desktop"
grep -q '^TryExec=stellar-wim$' "$desktop"
grep -q '^DesktopNames=stellar-wim;wlroots$' "$desktop"
grep -q '^\[preferred\]$' "$root/share/xdg-desktop-portal/stellar-wim-portals.conf"

echo "=== checking for build-only subproject paths ==="
python3 - "$build_dir" "$root" <<'PY'
import json
import os
import sys
from pathlib import Path

build_dir, root = map(Path, sys.argv[1:3])
options = {
    entry['name']: entry['value']
    for entry in json.loads(
        (build_dir / 'meson-info/intro-buildoptions.json').read_text())
}
plan = json.loads(
    (build_dir / 'meson-info/intro-install_plan.json').read_text())


def expand(destination):
    libdir = str(options.get('libdir', 'lib'))
    values = dict(options)
    values.setdefault('libdir_shared', libdir)
    values.setdefault('libdir_static', libdir)
    values.setdefault('prefix', '/usr/local')
    for key, value in values.items():
        destination = destination.replace('{' + key + '}', str(value))
    return destination


prefix = Path(str(options.get('prefix', '/usr/local'))).parts[1:]
dest_parts = list(root.parts)
if prefix and dest_parts[-len(prefix):] == prefix:
    dest_parts = dest_parts[:-len(prefix)]
destdir_root = Path(*dest_parts) if dest_parts else Path('/')

leftovers = []
for entries in plan.values():
    if isinstance(entries, dict):
        records = entries.values()
    elif isinstance(entries, list):
        records = entries
    else:
        continue
    for record in records:
        if not isinstance(record, dict) or not record.get('subproject'):
            continue
        destination = record.get('destination')
        if not destination:
            continue
        target = Path(expand(destination))
        if target.is_absolute():
            target = destdir_root / target.relative_to('/')
        else:
            target = root / target
        if target.exists() or target.is_symlink():
            leftovers.append(str(target))

if leftovers:
    print('build-only subproject files were installed:', file=sys.stderr)
    for path in leftovers[:50]:
        print('  ' + path, file=sys.stderr)
    sys.exit(1)
PY

echo "=== checking for Labwc-only installed paths ==="
for pattern in '*labwc*' '*labnag*' '*lab-sensible-terminal*'; do
	if find "$root" -name "$pattern" | grep -q .; then
		echo "Labwc-only path matching $pattern was installed" >&2
		find "$root" -name "$pattern" >&2
		exit 1
	fi
done

echo "=== checking for empty directories left by build-only subprojects ==="
empty=$(find "$root" -type d -empty | head -1)
if [ -n "$empty" ]; then
	echo "empty directory left in install tree: $empty" >&2
	exit 1
fi

target=$(find "$root" -name 'stellar-wim-session.target' -type f 2>/dev/null | head -1)
if [ -n "$target" ]; then
	grep -q 'Description=Stellar WiM session' "$target"
	echo "systemd session target installed: $target"
else
	echo "systemd session target not installed (systemd pkg-config unavailable)"
fi

if command -v dpkg-parsechangelog >/dev/null 2>&1; then
	source_root=$(cd "$(dirname "$0")/.." && pwd)
	changelog=$source_root/debian/changelog
	package=$(dpkg-parsechangelog -l "$changelog" -S Source)
	version=$(dpkg-parsechangelog -l "$changelog" -S Version)
	project_version=$(sed -n "s/^[[:space:]]*version: '\([^']*\)',/\1/p" \
		"$source_root/meson.build" | head -1)
	if [ -z "$project_version" ]; then
		echo "could not determine project version from meson.build" >&2
		exit 1
	fi
	expected_version=$project_version-1
	if [ "$package" != stellar-wim ] || [ "$version" != "$expected_version" ]; then
		echo "unexpected Debian metadata: $package $version (expected stellar-wim $expected_version)" >&2
		exit 1
	fi
	echo "Debian metadata: $package $version"
fi

echo "staged install OK: $root"
