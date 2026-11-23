#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# End-to-end checks for advanced tab/tiling/effects behaviour:
#  - pre-tiling floating geometry is restored when tiling is disabled
#  - minimizing the active tab activates a visible sibling
#  - compositor-requested close is animated before the client is closed
#  - hybrid mode keeps rule-exempt windows floating
#  - headless compositor reports multiple independent outputs
set -eu

compositor=$1
ctl_bin=$2
case "$ctl_bin" in
	/*) ctl_bin_abs=$ctl_bin ;;
	*) ctl_bin_abs=$PWD/$ctl_bin ;;
esac
ctl_bin=$ctl_bin_abs
tmp=$(mktemp -d)
comp_pid=
client_pid=
hybrid_client_pid=

cleanup() {
	if [ -n "$hybrid_client_pid" ]; then
		kill "$hybrid_client_pid" 2>/dev/null || true
		wait "$hybrid_client_pid" 2>/dev/null || true
	fi
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
sock="$tmp/runtime/stellar-advanced.sock"

write_config() {
	cat > "$tmp/config/stellar-wim/rc.xml" <<EOF
<labwc_config>
  <stellar-wim>
    <profile>enhanced</profile>
    <ipc enabled="true"/>
    <tabs enabled="true"/>
    <tiling enabled="true" mode="$1"/>
    <animations enabled="true"/>
    <opacity enabled="true" level="0.9"/>
  </stellar-wim>
  $2
</labwc_config>
EOF
}
write_config master-stack ''

env_common="XDG_CONFIG_HOME=$tmp/config"
env_common="$env_common XDG_CONFIG_DIRS=$tmp/empty"
env_common="$env_common XDG_DATA_HOME=$tmp/data"
env_common="$env_common XDG_RUNTIME_DIR=$tmp/runtime"
env_common="$env_common STELLAR_WIM_IPC_SOCKET=$sock"
env_common="$env_common WLR_BACKENDS=headless"
env_common="$env_common WLR_HEADLESS_OUTPUTS=2"

# shellcheck disable=SC2086
env $env_common "$compositor" -d >"$tmp/compositor.log" 2>&1 &
comp_pid=$!
i=0
while [ ! -S "$sock" ]; do
	if ! kill -0 "$comp_pid" 2>/dev/null; then
		echo "compositor exited before opening IPC" >&2
		cat "$tmp/compositor.log" >&2
		exit 1
	fi
	i=$((i + 1))
	[ "$i" -lt 50 ] || { echo "timed out waiting for IPC" >&2; exit 1; }
	sleep 0.1
done

ctl() {
	# shellcheck disable=SC2086
	env $env_common timeout 5s "$ctl_bin" "$@"
}

echo "=== multiple headless outputs ==="
ctl outputs list | python3 -c '
import json,sys
outs = json.load(sys.stdin)
assert len(outs) == 2, outs
assert all(o["enabled"] and o["width"] > 0 and o["height"] > 0 for o in outs)
print(len(outs), "outputs")
'

if ! python3 -c 'import gi; gi.require_version("Gtk", "3.0"); from gi.repository import Gtk' >/dev/null 2>&1; then
	echo "GTK 3 not available; advanced live tests skipped" >&2
	exit 0
fi

# shellcheck disable=SC2086
env $env_common python3 "$(dirname "$0")/spawn-gtk-windows.py" 3 stellar-adv >"$tmp/client.log" 2>&1 &
client_pid=$!
i=0
while [ "$i" -lt 100 ]; do
	count=$(ctl windows list 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)
	[ "$count" -ge 3 ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$count" -ge 3 ] || { echo "timed out waiting for GTK windows" >&2; cat "$tmp/client.log" >&2; exit 1; }

echo "=== tiling then restoring pre-tiling geometry ==="
# Start from a known floating state, then enable tiling via the enhanced profile.
ctl profile set minimal >/dev/null
sleep 0.4
ctl windows list > "$tmp/before.json"
ctl profile set enhanced >/dev/null
sleep 0.4
ctl tiling status | python3 -c '
import json,sys
d = json.load(sys.stdin)
assert d["enabled"] is True and d["mode"] == "master-stack", d
assert len(d["outputs"]) == 2, d
assert all({"name", "managed", "mode"} <= set(o) for o in d["outputs"]), d
'
ctl windows list > "$tmp/tiled.json"
python3 - "$tmp/before.json" "$tmp/tiled.json" <<'PY'
import json,sys
before = {w["id"]: w for w in json.load(open(sys.argv[1]))}
tiled = {w["id"]: w for w in json.load(open(sys.argv[2]))}
assert before.keys() == tiled.keys()
def box(w): return (w["x"], w["y"], w["width"], w["height"])
assert any(box(before[i]) != box(tiled[i]) for i in before), "tiling changed no geometry"
PY
ctl profile set minimal >/dev/null
sleep 0.4
ctl windows list > "$tmp/restored.json"
python3 - "$tmp/before.json" "$tmp/restored.json" <<'PY'
import json,sys
before = {w["id"]: w for w in json.load(open(sys.argv[1]))}
after = {w["id"]: w for w in json.load(open(sys.argv[2]))}
assert before.keys() == after.keys()
def box(w): return (w["x"], w["y"], w["width"], w["height"])
bad = [i for i in before if box(before[i]) != box(after[i])]
assert not bad, f"geometry not restored for {bad}"
PY
ctl profile set enhanced >/dev/null
sleep 0.3

echo "=== TILING TOGGLE over IPC ==="
enabled_before=$(ctl tiling status | python3 -c 'import json,sys; print(json.load(sys.stdin)["enabled"])')
ctl tiling toggle >/dev/null
enabled_after=$(ctl tiling status | python3 -c 'import json,sys; print(json.load(sys.stdin)["enabled"])')
[ "$enabled_before" != "$enabled_after" ] || { echo "TILING TOGGLE had no effect" >&2; exit 1; }
ctl tiling toggle >/dev/null

echo "=== minimizing the active tab keeps the group usable ==="
ids=$(ctl windows list | python3 -c '
import json,sys
print(" ".join(str(w["id"]) for w in json.load(sys.stdin)))
')
set -- $ids
ctl tabs group "$1" "$2" "$3" >/dev/null
active=$(ctl tabs list | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["active"])')
ctl windows minimize "$active" >/dev/null
sleep 0.3
ctl tabs list > "$tmp/tabs-after-minimize.json"
python3 - "$tmp/tabs-after-minimize.json" "$active" <<'PY'
import json,sys
g = json.load(open(sys.argv[1]))[0]
old = int(sys.argv[2])
assert g["active"] != old, "active tab was not changed on minimize"
assert g["active"] in g["members"]
PY

echo "=== close animation defers the protocol close ==="
started=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["animations_started"])')
victim=$(ctl tabs list | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["active"])')
ctl windows close "$victim" >/dev/null
after=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["animations_started"])')
[ "$after" -gt "$started" ] || { echo "close did not start an animation" >&2; exit 1; }
# Switching to a profile with animations off mid-fade must still deliver the close.
ctl profile set minimal >/dev/null
i=0
while [ "$i" -lt 50 ]; do
	remaining=$(ctl windows list | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 3)
	[ "$remaining" -lt 3 ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$remaining" -lt 3 ] || { echo "close animation never completed" >&2; exit 1; }

echo "=== hybrid mode honours floating window rules ==="
# Close the first GTK process so the compositor no longer has three windows.
kill "$client_pid" 2>/dev/null || true
wait "$client_pid" 2>/dev/null || true
client_pid=
i=0
while [ "$i" -lt 50 ]; do
	count=$(ctl windows list 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)
	[ "$count" -eq 0 ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$count" -eq 0 ] || { echo "timed out waiting for first windows to close" >&2; exit 1; }

env $env_common python3 "$(dirname "$0")/spawn-gtk-windows.py" 2 stellar-hyb >>"$tmp/client.log" 2>&1 &
hybrid_client_pid=$!
i=0
while [ "$i" -lt 100 ]; do
	count=$(ctl windows list 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)
	[ "$count" -ge 2 ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$count" -ge 2 ] || { echo "timed out waiting for hybrid test windows" >&2; exit 1; }
# Start from floating so the exemption can be verified as "stayed floating".
ctl profile set minimal >/dev/null
sleep 0.4
ctl windows list > "$tmp/hybrid-before.json"
write_config hybrid '<windowRules><windowRule title="stellar-hyb-0" stellarTiling="floating"/></windowRules>'
kill -HUP "$comp_pid"
sleep 0.8
ctl tiling status | python3 -c '
import json,sys
d = json.load(sys.stdin)
assert d["mode"] == "hybrid", d
assert d["managed"] >= 1, d
'
ctl windows list > "$tmp/hybrid-after.json"
python3 - "$tmp/hybrid-before.json" "$tmp/hybrid-after.json" <<'PY'
import json,sys
def by_title(path): return {w["title"]: w for w in json.load(open(path))}
before, after = by_title(sys.argv[1]), by_title(sys.argv[2])
floating = before["stellar-hyb-0"]; now = after["stellar-hyb-0"]
def box(w): return (w["x"], w["y"], w["width"], w["height"])
assert box(floating) == box(now), (box(floating), box(now))
PY

echo "=== shutdown cleanup ==="
kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=
[ ! -e "$sock" ] || { echo "IPC socket was not removed" >&2; exit 1; }
