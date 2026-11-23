#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Regression test for the benchmark median helper (odd and even sample counts).
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=benchmark-lib.sh
. "$script_dir/benchmark-lib.sh"

tmp=$(mktemp -d /tmp/stellar-median.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cat >"$tmp/odd" <<'EOF'
label 181 0 102768
label 179 0 101628
label 180 0 103000
label 177 1 100000
label 200 2 110000
EOF
cat >"$tmp/even" <<'EOF'
label 10 0 100
label 20 0 200
EOF

check() {
	actual=$1
	expected=$2
	description=$3
	if [ "$actual" != "$expected" ]; then
		echo "$description: expected $expected, got $actual" >&2
		exit 1
	fi
}

check "$(median "$tmp/odd" 2)" 180 "odd startup median"
check "$(median "$tmp/odd" 3)" 0 "odd CPU median"
check "$(median "$tmp/odd" 4)" 102768 "odd RSS median"
check "$(median "$tmp/even" 2)" 15 "even startup median"
check "$(median "$tmp/even" 4)" 150 "even RSS median"

echo "benchmark median test passed"
