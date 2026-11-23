#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Verify fork identity: our own config dir wins over inherited Labwc paths,
# and the compositor starts with the headless backend.
set -eu

bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$tmp/runtime" "$tmp/config/stellar-wim" "$tmp/config/labwc"
mkdir -p "$tmp/system/stellar-wim" "$tmp/system/labwc"
mkdir -p "$tmp/data"
chmod 700 "$tmp/runtime"

printf '%s\n' '<labwc_config/>' > "$tmp/config/stellar-wim/rc.xml"
printf '%s\n' '<labwc_config/>' > "$tmp/config/labwc/rc.xml"
printf '%s\n' '<labwc_config/>' > "$tmp/system/stellar-wim/rc.xml"
printf '%s\n' '<labwc_config/>' > "$tmp/system/labwc/rc.xml"

"$bin" --version | grep -q '^Stellar WiM ' || {
	echo "unexpected version output" >&2
	exit 1
}

XDG_CONFIG_HOME="$tmp/config" \
XDG_CONFIG_DIRS="$tmp/system" \
XDG_DATA_HOME="$tmp/data" \
XDG_RUNTIME_DIR="$tmp/runtime" \
WLR_BACKENDS=headless \
	timeout 3s "$bin" -d >"$tmp/log" 2>&1 || true

grep -F "read config file $tmp/config/stellar-wim/rc.xml" "$tmp/log" || {
	echo "Stellar WiM rc.xml was not read from its own config dir" >&2
	cat "$tmp/log" >&2
	exit 1
}

if grep -F "read config file $tmp/config/labwc/rc.xml" "$tmp/log"; then
	echo "Labwc rc.xml must not be preferred over stellar-wim/rc.xml" >&2
	exit 1
fi

echo "identity config dir test passed"
