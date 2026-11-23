#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# End-to-end test for the optional Stellar WiM Fase 3 features.
set -eu

compositor=$1
ctl_bin=$2
case "$ctl_bin" in
	/*) ctl_bin_abs=$ctl_bin ;;
	*) ctl_bin_abs=$PWD/$ctl_bin ;;
esac
tmp=$(mktemp -d)
comp_pid=
client_pid=

cleanup() {
	if [ -n "$client_pid" ]; then
		kill "$client_pid" 2>/dev/null || true
		wait "$client_pid" 2>/dev/null || true
	fi
	if [ -n "$comp_pid" ]; then
		kill "$comp_pid" 2>/dev/null || true
		wait "$comp_pid" 2>/dev/null || true
	fi
	chmod 700 "$tmp/runtime" 2>/dev/null || true
	rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$tmp/runtime" "$tmp/config/stellar-wim" "$tmp/data" "$tmp/empty"
chmod 700 "$tmp/runtime"

sock="$tmp/runtime/stellar-features-test.sock"
cat > "$tmp/config/stellar-wim/rc.xml" <<'EOF'
<labwc_config>
  <stellar-wim>
    <ipc enabled="true"/>
    <tabs enabled="true"/>
    <tiling enabled="true"/>
  </stellar-wim>
</labwc_config>
EOF

env_common="XDG_CONFIG_HOME=$tmp/config"
env_common="$env_common XDG_CONFIG_DIRS=$tmp/empty"
env_common="$env_common XDG_DATA_HOME=$tmp/data"
env_common="$env_common XDG_RUNTIME_DIR=$tmp/runtime"
env_common="$env_common STELLAR_WIM_IPC_SOCKET=$sock"
env_common="$env_common WLR_BACKENDS=headless"

# shellcheck disable=SC2086
env $env_common "$compositor" -d >"$tmp/compositor.log" 2>&1 &
comp_pid=$!

i=0
while [ ! -S "$sock" ]; do
	if ! kill -0 "$comp_pid" 2>/dev/null; then
		echo "compositor exited before opening the IPC socket" >&2
		cat "$tmp/compositor.log" >&2
		exit 1
	fi
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "timed out waiting for $sock" >&2
		cat "$tmp/compositor.log" >&2
		exit 1
	}
	sleep 0.1
done

ctl() {
	# shellcheck disable=SC2086
	env $env_common timeout 5s "$ctl_bin" "$@"
}

echo "=== feature flags ==="
ctl version | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["features"]["ipc"] is True
assert d["features"]["tabs"] is True
assert d["features"]["tiling"] is True
'
ctl capabilities | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["tabs"] == 1 and d["tiling"] == 1
'

echo "=== tabs commands ==="
ctl tabs status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["enabled"] is True and d["groups"] == 0
'
ctl tabs list | python3 -c 'import json,sys; assert json.load(sys.stdin) == []'
if ctl tabs next >"$tmp/tabs-next.out" 2>"$tmp/tabs-next.err"; then
	echo "tabs next unexpectedly succeeded without a group" >&2
	exit 1
fi
grep -q 'active window is not in a tab group' "$tmp/tabs-next.err"

if ctl tabs group 1 2 >"$tmp/tabs-group.out" 2>"$tmp/tabs-group.err"; then
	echo "tabs group unexpectedly succeeded without windows" >&2
	exit 1
fi
grep -q 'window not found' "$tmp/tabs-group.err"

echo "=== tiling commands ==="
ctl tiling status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["enabled"] is True and d["managed"] == 0
'
ctl tiling reapply | python3 -c 'import json,sys; assert json.load(sys.stdin)["ok"] is True'
ctl windows list | python3 -c 'import json,sys; assert json.load(sys.stdin) == []'

echo "=== tiling mode selection ==="
ctl tiling set grid | python3 -c '
import json,sys
d = json.load(sys.stdin)
assert d["mode"] == "grid" and d["ok"] is True
'
ctl tiling status | python3 -c '
import json,sys
d = json.load(sys.stdin)
assert d["mode"] == "grid"
'
ctl tiling set horizontal >/dev/null
ctl tiling status | python3 -c '
import json,sys
assert json.load(sys.stdin)["mode"] == "horizontal"
'
ctl tiling set bogus >"$tmp/mode.err" 2>&1 || true
grep -q 'unknown tiling mode' "$tmp/mode.err"

echo "=== real windows: three tabs and shared geometry ==="
if ! python3 -c 'import gi; gi.require_version("Gtk", "3.0"); from gi.repository import Gtk' >/dev/null 2>&1; then
	echo "GTK 3 not available; live window test skipped" >&2
else
	# shellcheck disable=SC2086
	env $env_common python3 "$(dirname "$0")/spawn-gtk-windows.py" 3 stellar-tab >"$tmp/client.log" 2>&1 &
	client_pid=$!

	i=0
	while [ "$i" -lt 100 ]; do
		count=$(ctl windows list 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)
		[ "$count" -ge 3 ] && break
		i=$((i + 1))
		sleep 0.1
	done
	[ "$count" -ge 3 ] || {
		echo "timed out waiting for three GTK windows" >&2
		cat "$tmp/client.log" >&2
		exit 1
	}

	ids=$(ctl windows list | python3 -c '
import json,sys
print(" ".join(str(w["id"]) for w in json.load(sys.stdin)))
')
	set -- $ids
	first_id=$1
	second_id=$2
	third_id=$3
	ctl tabs group "$first_id" "$second_id" | python3 -c '
import json,sys
assert json.load(sys.stdin)["ok"] is True
'
	ctl tabs list | python3 -c '
import json,sys
groups = json.load(sys.stdin)
assert len(groups) == 1 and len(groups[0]["members"]) == 2, groups
'
	# Extend an existing group with a third mapped window.
	ctl tabs group "$second_id" "$third_id" | python3 -c '
import json,sys
assert json.load(sys.stdin)["ok"] is True
'
	ctl tabs list | python3 -c '
import json,sys
groups = json.load(sys.stdin)
assert len(groups) == 1
assert len(groups[0]["members"]) == 3, groups
assert groups[0]["active"] in groups[0]["members"]
'
	active_before=$(ctl tabs list | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["active"])')
	animations_before=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["animations_started"])')
	ctl tabs next >/dev/null
	animations_after=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["animations_started"])')
	if [ "$animations_after" -le "$animations_before" ]; then
		echo "tab switch did not start a fade" >&2
		exit 1
	fi
	active_after=$(ctl tabs list | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["active"])')
	[ "$active_before" != "$active_after" ] || {
		echo "tabs next did not change the active window" >&2
		exit 1
	}
	ctl windows list | python3 -c '
import json,sys
windows = json.load(sys.stdin)
assert len(windows) == 3
assert all(w["mapped"] is True for w in windows)
'
	first_id=$(printf '%s\n' "$ids" | awk '{print $1}')
	ctl tabs detach "$first_id" >/dev/null
	ctl tabs list | python3 -c '
import json,sys
groups = json.load(sys.stdin)
assert len(groups) == 1 and len(groups[0]["members"]) == 2, groups
'
	kill "$client_pid" 2>/dev/null || true
	wait "$client_pid" 2>/dev/null || true
	client_pid=
fi

echo "=== shutdown cleanup ==="
kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=
if [ -e "$sock" ]; then
	echo "IPC socket was not removed on shutdown" >&2
	exit 1
fi
