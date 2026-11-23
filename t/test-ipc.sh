#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# End-to-end test for the Stellar IPC server and stellar-wimctl.
set -eu

compositor=$1
ctl_bin=$2
case "$ctl_bin" in
	/*) ctl_bin_abs=$ctl_bin ;;
	*) ctl_bin_abs=$PWD/$ctl_bin ;;
esac
tmp=$(mktemp -d)
comp_pid=
subscriber_pid=
config_subscriber_pid=

cleanup() {
	if [ -n "$config_subscriber_pid" ]; then
		kill "$config_subscriber_pid" 2>/dev/null || true
		wait "$config_subscriber_pid" 2>/dev/null || true
	fi
	if [ -n "$subscriber_pid" ]; then
		kill "$subscriber_pid" 2>/dev/null || true
		wait "$subscriber_pid" 2>/dev/null || true
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

sock="$tmp/runtime/stellar-ipc-test.sock"
cat > "$tmp/config/stellar-wim/rc.xml" <<'EOF'
<labwc_config>
  <stellar-wim><ipc enabled="true"/></stellar-wim>
  <desktops><number>2</number></desktops>
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

wait_for_socket() {
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
}

wait_for_log() {
	pattern=$1
	file=$2
	i=0
	until grep -q "$pattern" "$file"; do
		i=$((i + 1))
		[ "$i" -lt 50 ] || {
			echo "expected log line not found: $pattern" >&2
			cat "$file" >&2
			exit 1
		}
		sleep 0.1
	done
}

wait_for_socket

mode=$(stat -c '%a' "$sock")
if [ "$mode" != 600 ]; then
	echo "IPC socket mode is $mode, expected 600" >&2
	exit 1
fi

ctl() {
	# shellcheck disable=SC2086
	env $env_common timeout 5s "$ctl_bin" "$@"
}

ctl version | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["upstream"] == "Labwc 0.20.3"
assert d["version"]
assert d["features"]["ipc"] is True
assert d["features"]["tabs"] is False
'
ctl capabilities | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["windows"] == 1 and d["desktops"] == 1 and d["outputs"] == 1
assert d["tabs"] == 0 and d["tiling"] == 0
'
ctl ping | python3 -c 'import json,sys; assert json.load(sys.stdin)["pong"] is True'
ctl config get | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["animations"] is True and d["opacity"] is True
assert d["tabs"] is False and d["tiling"] is False
assert d["profile"] == "custom" and d["corner_radius"] > 0
'
ctl config list | python3 -c '
import json, sys
d = json.load(sys.stdin)
keys = {entry["key"] for entry in d}
assert {"effects.animations", "theme.corner-radius", "profile", "ipc.enabled"} <= keys
assert all(entry["applies"] in ("live", "reload") for entry in d)
'
ctl config get effects.animations | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["key"] == "effects.animations" and d["value"] is True
assert d["type"] == "boolean" and d["persisted"] is True
'
ctl config status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["config_file"].endswith("/stellar-wim/rc.xml")
assert d["writable"] is True and d["persistent"] is True
'

echo "=== configuration changes are validated, saved and applied ==="
# shellcheck disable=SC2086
env $env_common "$ctl_bin" events subscribe >"$tmp/config-events" 2>"$tmp/config-events.err" &
config_subscriber_pid=$!
i=0
until grep -q '"subscribed":true' "$tmp/config-events"; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || { echo "config subscription failed" >&2; exit 1; }
	sleep 0.1
done
ctl config set effects.animations false | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["applied"] is True and d["persisted"] is True and d["reload"] is False
'
ctl effects status | python3 -c 'import json,sys; assert json.load(sys.stdin)["animations"] is False'
grep -q 'animations="no"' "$tmp/config/stellar-wim/rc.xml"
i=0
until grep -q '"type":"config".*"key":"effects.animations"' "$tmp/config-events"; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || { echo "config change event not received" >&2; cat "$tmp/config-events" >&2; exit 1; }
	sleep 0.1
done
ctl config reset theme.name | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d == dict(target="theme.name", reset=True, persisted=True, reload=True, ok=True)
'
i=0
until grep -q '"key":"theme.name"' "$tmp/config-events"; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || { echo "theme reset event not received" >&2; exit 1; }
	sleep 0.1
done
python3 - "$tmp/config-events" <<'PY'
import json, sys
events = [json.loads(line[6:]) for line in open(sys.argv[1]) if line.startswith("EVENT ")]
assert any(e.get("key") == "theme.name" and e.get("value") == "" for e in events)
PY
kill "$config_subscriber_pid" 2>/dev/null || true
wait "$config_subscriber_pid" 2>/dev/null || true

ctl config reset effects.animations >/dev/null
ctl effects status | python3 -c 'import json,sys; assert json.load(sys.stdin)["animations"] is True'
ctl config set theme.corner-radius 20 | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["reload"] is True and d["persisted"] is True and d["applied"] is False
'
sleep 0.4
ctl config get theme.corner-radius | python3 -c 'import json,sys; assert json.load(sys.stdin)["value"] == 20'
grep -q '<cornerRadius>20</cornerRadius>' "$tmp/config/stellar-wim/rc.xml"
if ctl config set theme.corner-radius 99 >"$tmp/cfg-bad.out" 2>"$tmp/cfg-bad.err"; then
	echo "out-of-range configuration value was accepted" >&2
	exit 1
fi
if ctl config set no.such.key true >"$tmp/cfg-key.out" 2>"$tmp/cfg-key.err"; then
	echo "unknown configuration key was accepted" >&2
	exit 1
fi
if ctl config set theme.name Mi/Tema >"$tmp/cfg-theme.out" 2>"$tmp/cfg-theme.err"; then
	echo "theme name with a path separator was accepted" >&2
	exit 1
fi
if ctl config set theme.name 'Mi Tema' >"$tmp/cfg-theme-space.out" 2>"$tmp/cfg-theme-space.err"; then
	echo "theme name with spaces was accepted" >&2
	exit 1
fi
ctl config reset section all >/dev/null
sleep 0.4
ctl config get | python3 -c 'import json,sys; assert json.load(sys.stdin)["corner_radius"] == 14'

echo "=== concurrent configuration writes are serialized ==="
ctl config set effects.animations false >"$tmp/cfg-par-1.out" 2>"$tmp/cfg-par-1.err" &
p1=$!
ctl config set effects.opacity false >"$tmp/cfg-par-2.out" 2>"$tmp/cfg-par-2.err" &
p2=$!
ctl config set tiling.columns 3 >"$tmp/cfg-par-3.out" 2>"$tmp/cfg-par-3.err" &
p3=$!
wait "$p1"
wait "$p2"
wait "$p3"
for n in 1 2 3; do
	python3 - "$tmp/cfg-par-$n.out" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["ok"] is True and d["persisted"] is True
PY
done
ctl config get | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["animations"] is False and d["opacity"] is False
assert d["tiling.columns"] == 3
'
grep -q 'animations="no"' "$tmp/config/stellar-wim/rc.xml"
grep -q 'opacity="no"' "$tmp/config/stellar-wim/rc.xml"
grep -q '<columns>3</columns>' "$tmp/config/stellar-wim/rc.xml"
ctl effects status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["animations"] is False and d["opacity"] is False
'
ctl config reset section all >/dev/null
sleep 0.4

echo "=== a client disconnecting mid-write does not lose the change ==="
python3 - "$sock" <<'PY'
import socket, struct, sys
payload = b"CONFIG SET theme.corner-radius 22"
frame = struct.pack("<II", 0x5357494d, len(payload)) + payload
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.sendall(frame)
s.close()
PY
i=0
until grep -q '<cornerRadius>22</cornerRadius>' "$tmp/config/stellar-wim/rc.xml"; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "disconnected client's configuration write was lost" >&2
		exit 1
	}
	sleep 0.1
done
sleep 0.4
ctl config get theme.corner-radius | python3 -c '
import json, sys
assert json.load(sys.stdin)["value"] == 22
'
ctl config reset section all >/dev/null
sleep 0.4

echo "=== explicit reload is ordered behind queued writes ==="
python3 "$(dirname "$0")/test-config-queue.py" "$sock" "$tmp/config/stellar-wim/rc.xml"
ctl config set theme.corner-radius 19 >"$tmp/cfg-race-1.out" 2>"$tmp/cfg-race-1.err" &
rp1=$!
ctl config set tiling.columns 5 >"$tmp/cfg-race-2.out" 2>"$tmp/cfg-race-2.err" &
rp2=$!
ctl config reload >"$tmp/cfg-race-reload.out" 2>"$tmp/cfg-race-reload.err" &
rp3=$!
wait "$rp1"
wait "$rp2"
wait "$rp3"
for f in "$tmp/cfg-race-1.out" "$tmp/cfg-race-2.out" "$tmp/cfg-race-reload.out"; do
	python3 - "$f" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["ok"] is True
PY
done
grep -q '<cornerRadius>19</cornerRadius>' "$tmp/config/stellar-wim/rc.xml"
grep -q '<columns>5</columns>' "$tmp/config/stellar-wim/rc.xml"
ctl config get | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["corner_radius"] == 19 and d["tiling.columns"] == 5
'
ctl config reset section all >/dev/null
sleep 0.4

echo "=== keybindings and window rules are managed safely ==="
ctl config keybindings list | python3 -c 'import json,sys; assert json.load(sys.stdin) == []'
ctl config keybindings set W-F12 StellarTilingToggle | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["ok"] is True and d["reload"] is True and d["persisted"] is True
'
sleep 0.4
grep -q '<keybind key="W-F12">' "$tmp/config/stellar-wim/rc.xml"
grep -q '<action name="StellarTilingToggle"/>' "$tmp/config/stellar-wim/rc.xml"
ctl config keybindings list | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert len(d) == 1 and d[0]["key"] == "W-F12"
assert d[0]["actions"] == ["StellarTilingToggle"]
'
if ctl config keybindings set W-F9 Execute >"$tmp/cfg-exec.out" 2>"$tmp/cfg-exec.err"; then
	echo "unsafe Execute action was accepted" >&2
	exit 1
fi
ctl config keybindings remove W-F12 >/dev/null
sleep 0.4
! grep -q '<keybind key="W-F12">' "$tmp/config/stellar-wim/rc.xml"
ctl config keybindings list | python3 -c 'import json,sys; assert json.load(sys.stdin) == []'

ctl config rules list | python3 -c 'import json,sys; assert json.load(sys.stdin) == []'
ctl config rules set foot stellarTilingMode horizontal | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["ok"] is True and d["reload"] is True
'
sleep 0.4
grep -q 'identifier="foot"' "$tmp/config/stellar-wim/rc.xml"
grep -q 'stellarTilingMode="horizontal"' "$tmp/config/stellar-wim/rc.xml"
ctl config rules list | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert len(d) == 1 and d[0]["attributes"]["identifier"] == "foot"
assert d[0]["attributes"]["stellarTilingMode"] == "horizontal"
'
if ctl config rules set foot unknownProperty yes >"$tmp/cfg-rule.out" 2>"$tmp/cfg-rule.err"; then
	echo "unknown window rule property was accepted" >&2
	exit 1
fi
ctl config rules remove foot stellarTilingMode >/dev/null
sleep 0.4
! grep -q 'stellarTilingMode' "$tmp/config/stellar-wim/rc.xml"
ctl config rules set foot skipTaskbar yes >/dev/null
sleep 0.4
ctl config rules reset >/dev/null
sleep 0.4
ctl config rules list | python3 -c 'import json,sys; assert json.load(sys.stdin) == []'

ctl tabs status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["enabled"] is False and d["groups"] == 0
'
ctl tiling status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["enabled"] is False and d["managed"] == 0
'
if ctl tabs list >"$tmp/tabs-disabled.out" 2>"$tmp/tabs-disabled.err"; then
	echo "tabs list unexpectedly succeeded while tabs are disabled" >&2
	exit 1
fi
grep -q 'tabs are disabled' "$tmp/tabs-disabled.err"
ctl windows list | python3 -c 'import json,sys; assert isinstance(json.load(sys.stdin), list)'
ctl windows active | python3 -c 'import json,sys; assert json.load(sys.stdin) is None'
ctl desktops list | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert len(d) == 2
assert d[0]["active"] is True and d[1]["active"] is False
'
ctl outputs list | python3 -c 'import json,sys; assert len(json.load(sys.stdin)) >= 1'
ctl effects status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["animations"] is True
assert d["opacity"] is True
assert d["blur"] is False
assert d["enabled"] is True
'

ctl desktops switch 2 >/dev/null
ctl desktops list | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d[1]["active"] is True and d[0]["active"] is False
'

if ctl bogus command >"$tmp/bogus.out" 2>"$tmp/bogus.err"; then
	echo "unknown IPC command unexpectedly succeeded" >&2
	exit 1
fi
grep -q 'unknown command' "$tmp/bogus.err" || {
	echo "unexpected error for unknown command" >&2
	cat "$tmp/bogus.err" >&2
	exit 1
}

if ctl windows activate 999999 >"$tmp/missing.out" 2>"$tmp/missing.err"; then
	echo "unknown window id unexpectedly succeeded" >&2
	exit 1
fi
grep -q 'window not found' "$tmp/missing.err" || {
	echo "unexpected error for unknown window id" >&2
	cat "$tmp/missing.err" >&2
	exit 1
}

# Subscription receives a real workspace notification.
# shellcheck disable=SC2086
env $env_common "$ctl_bin" events subscribe >"$tmp/events" 2>"$tmp/events.err" &
subscriber_pid=$!
i=0
until grep -q '"subscribed":true' "$tmp/events"; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "IPC subscription was not established" >&2
		cat "$tmp/events.err" >&2
		exit 1
	}
	sleep 0.1
done
ctl desktops switch 1 >/dev/null
i=0
until grep -q '"type":"workspace"' "$tmp/events"; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "workspace IPC event was not received" >&2
		cat "$tmp/events" >&2
		exit 1
	}
	sleep 0.1
done
grep -q '"index":1' "$tmp/events" || {
	echo "workspace event had an unexpected index" >&2
	cat "$tmp/events" >&2
	exit 1
}
kill "$subscriber_pid" 2>/dev/null || true
wait "$subscriber_pid" 2>/dev/null || true
subscriber_pid=

# Peer credentials are checked in addition to file permissions. Prefer
# dropping privileges directly; fall back to passwordless sudo -u nobody.
probe_foreign_uid() {
	chmod 711 "$tmp"
	chmod 711 "$tmp/runtime"
	chmod 666 "$sock"
	if "$@" env STELLAR_WIM_IPC_SOCKET="$sock" \
			timeout 5s "$ctl_bin_abs" ping >"$tmp/foreign.out" 2>&1; then
		echo "a different uid was allowed to use IPC" >&2
		cat "$tmp/foreign.out" >&2
		exit 1
	fi
	# The server closes foreign-uid connections as soon as it accepts them,
	# so the client may report a closed connection either while sending or
	# while reading. The authoritative check is the server-side log below.
	chmod 700 "$tmp/runtime"
	chmod 700 "$tmp"
	chmod 600 "$sock"
	wait_for_log 'rejected IPC client with different uid' "$tmp/compositor.log"
}

if command -v setpriv >/dev/null 2>&1 &&
		setpriv --reuid=65534 --regid=65534 --clear-groups true >/dev/null 2>&1; then
	probe_foreign_uid setpriv --reuid=65534 --regid=65534 --clear-groups
elif command -v sudo >/dev/null 2>&1 &&
		sudo -n -u nobody env true >/dev/null 2>&1; then
	probe_foreign_uid sudo -n -u nobody
else
	echo "IPC peer-credential probe skipped: no way to run as another uid"
fi

echo "=== a second instance must not steal or unlink a live socket ==="
# shellcheck disable=SC2086
env $env_common "$compositor" -d >"$tmp/second.log" 2>&1 &
second_pid=$!
wait_for_log 'IPC socket already in use' "$tmp/second.log"
kill -TERM "$second_pid" 2>/dev/null || true
wait "$second_pid" || true
[ -S "$sock" ]
ctl version >/dev/null

echo "=== disabling IPC still returns its reply before closing the socket ==="
ctl config set ipc.enabled false | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["ok"] is True and d["persisted"] is True and d["reload"] is True
'
i=0
while [ -e "$sock" ]; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "IPC socket was not removed after ipc.enabled=false" >&2
		exit 1
	}
	sleep 0.1
done
wait_for_log 'Stellar IPC will stop after pending replies' "$tmp/compositor.log"
if ctl ping >"$tmp/ipc-disabled.out" 2>"$tmp/ipc-disabled.err"; then
	echo "IPC remained reachable after ipc.enabled=false" >&2
	exit 1
fi
# Restore IPC for the remaining shutdown checks. Only the test edits rc.xml
# directly; normal clients must always use the Configuration API.
cat > "$tmp/config/stellar-wim/rc.xml" <<'EOF'
<labwc_config><stellar-wim><ipc enabled="true"/></stellar-wim></labwc_config>
EOF
kill -HUP "$comp_pid"
wait_for_socket
ctl version >/dev/null

kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=
if [ -e "$sock" ]; then
	echo "IPC socket was not removed on shutdown" >&2
	exit 1
fi

# Disabled IPC must not create a socket at all.
cat > "$tmp/config/stellar-wim/rc.xml" <<'EOF'
<labwc_config><stellar-wim><ipc enabled="false"/></stellar-wim></labwc_config>
EOF
# shellcheck disable=SC2086
env $env_common "$compositor" -d >"$tmp/disabled.log" 2>&1 &
comp_pid=$!
sleep 1
if [ -e "$sock" ]; then
	echo "IPC socket exists even though IPC is disabled" >&2
	exit 1
fi
wait_for_log 'Stellar IPC disabled by configuration' "$tmp/disabled.log"
kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=

echo "=== stale socket is replaced without blocking ==="
cat > "$tmp/config/stellar-wim/rc.xml" <<'EOF'
<labwc_config><stellar-wim><ipc enabled="true"/></stellar-wim></labwc_config>
EOF
python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.bind(sys.argv[1]); s.close()' "$sock"
[ -S "$sock" ]
# shellcheck disable=SC2086
env $env_common "$compositor" -d >"$tmp/stale.log" 2>&1 &
comp_pid=$!
wait_for_log 'Stellar IPC listening' "$tmp/stale.log"
ctl version >/dev/null
kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=
[ ! -e "$sock" ] || {
	echo "stale socket was not cleaned up on shutdown" >&2
	exit 1
}

echo "IPC end-to-end test passed"
