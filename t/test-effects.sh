#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# End-to-end test for opt-in Stellar WiM visual effects and their idle cost.
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

sock="$tmp/runtime/stellar-effects-test.sock"
rc="$tmp/config/stellar-wim/rc.xml"

write_default_rc() {
	cat > "$rc" <<'EOF'
<labwc_config>
  <stellar-wim>
    <ipc enabled="true"/>
  </stellar-wim>
</labwc_config>
EOF
}

write_effects_rc() {
	level=$1
	cat > "$rc" <<EOF
<labwc_config>
  <theme>
    <dropShadows>yes</dropShadows>
  </theme>
  <stellar-wim>
    <ipc enabled="true"/>
    <effects animations="yes" opacity="yes" opacity-level="$level"/>
  </stellar-wim>
</labwc_config>
EOF
}

write_effects_off_rc() {
	cat > "$rc" <<'EOF'
<labwc_config>
  <theme>
    <dropShadows>no</dropShadows>
  </theme>
  <stellar-wim>
    <ipc enabled="true"/>
    <effects animations="no" opacity="no" opacity-level="0.90"/>
  </stellar-wim>
</labwc_config>
EOF
}

write_default_rc

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
	env $env_common timeout 5s "$ctl_bin_abs" "$@"
}

reload() {
	kill -HUP "$comp_pid"
	i=0
	while [ "$i" -lt 50 ]; do
		if ctl ping >/dev/null 2>&1; then
			return 0
		fi
		i=$((i + 1))
		sleep 0.1
	done
	echo "compositor stopped responding after SIGHUP" >&2
	cat "$tmp/compositor.log" >&2
	exit 1
}

wait_for_status() {
	field=$1
	expected=$2
	i=0
	while [ "$i" -lt 50 ]; do
		if ctl effects status 2>/dev/null | python3 -c '
import json, sys
field, expected = sys.argv[1], sys.argv[2]
d = json.load(sys.stdin)
actual = "true" if d[field] is True else "false" if d[field] is False else str(d[field])
sys.exit(0 if actual == expected else 1)
' "$field" "$expected"; then
			return 0
		fi
		i=$((i + 1))
		sleep 0.1
	done
	echo "timed out waiting for effects status $field=$expected" >&2
	ctl effects status >&2 || true
	exit 1
}

echo "=== default: animations, shadows and opacity are on ==="
ctl effects status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["animations"] is True
assert d["opacity"] is True
assert d["shadows"] is True
assert d["blur"] is False
assert d["enabled"] is True
assert d["active_animations"] == 0
assert d["timer_armed"] is False
assert d["opacity_level"] == 0.9
'
ctl version | python3 -c 'import json,sys; assert json.load(sys.stdin)["features"]["animations"] is True'
ctl capabilities | python3 -c 'import json,sys; assert json.load(sys.stdin)["effects"] == 1'

echo "=== no idle render commits with default effects and no clients ==="
before=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["frame_commits"])')
sleep 0.5
after=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["frame_commits"])')
if [ "$before" != "$after" ]; then
	echo "frame_commits changed while idle with default effects: $before -> $after" >&2
	exit 1
fi

echo "=== custom opacity level after SIGHUP ==="
write_effects_rc 0.80
reload
wait_for_status animations true
ctl effects status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["animations"] is True
assert d["opacity"] is True
assert d["shadows"] is True
assert d["blur"] is False
assert d["enabled"] is True
assert d["opacity_level"] == 0.8
assert d["active_animations"] == 0
assert d["timer_armed"] is False
'
ctl version | python3 -c 'import json,sys; assert json.load(sys.stdin)["features"]["animations"] is True'
ctl capabilities | python3 -c 'import json,sys; assert json.load(sys.stdin)["effects"] == 1'

echo "=== no idle render commits or timer with effects enabled but no clients ==="
before=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["frame_commits"])')
sleep 0.5
after=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["frame_commits"])')
if [ "$before" != "$after" ]; then
	echo "frame_commits changed while idle with effects on: $before -> $after" >&2
	exit 1
fi
ctl effects status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["timer_armed"] is False and d["active_animations"] == 0
'

echo "=== opacity level is clamped ==="
write_effects_rc 3.5
reload
wait_for_status opacity_level 1.0
write_effects_rc -2.0
reload
wait_for_status opacity_level 0.0

echo "=== explicitly disabling effects leaves no timer ==="
write_effects_off_rc
reload
wait_for_status animations false
ctl effects status | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["opacity"] is False and d["shadows"] is False and d["enabled"] is False
assert d["active_animations"] == 0 and d["timer_armed"] is False
assert d["opacity_level"] == 1.0
'

echo "=== minimize fade-out with a real window ==="
if python3 -c 'import gi; gi.require_version("Gtk", "3.0"); from gi.repository import Gtk' >/dev/null 2>&1; then
	write_default_rc
	reload
	wait_for_status animations true
	# shellcheck disable=SC2086
	env $env_common python3 "$(dirname "$0")/spawn-gtk-windows.py" 1 stellar-fade >"$tmp/fade-client.log" 2>&1 &
	client_pid=$!
	i=0
	while [ "$i" -lt 100 ]; do
		count=$(ctl windows list 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)
		[ "$count" -ge 1 ] && break
		i=$((i + 1))
		sleep 0.1
	done
	[ "$count" -ge 1 ] || {
		echo "timed out waiting for GTK window" >&2
		cat "$tmp/fade-client.log" >&2
		exit 1
	}
	id=$(ctl windows list | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["id"])')
	# Let the map fade-in settle so the counter change below is only the fade-out.
	sleep 0.4
	started=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["animations_started"])')
	ctl windows minimize "$id" >/dev/null
	i=0
	while [ "$i" -lt 50 ]; do
		running=$(ctl effects status | python3 -c '
import json,sys
d = json.load(sys.stdin)
print("1" if d["active_animations"] >= 1 and d["timer_armed"] is True else "0")
' 2>/dev/null || echo 0)
		[ "$running" = 1 ] && break
		i=$((i + 1))
		sleep 0.02
	done
	[ "$running" = 1 ] || {
		echo "minimize did not start a fade-out" >&2
		ctl effects status >&2 || true
		exit 1
	}
	sleep 0.4
	ctl effects status | python3 -c '
import json,sys
d = json.load(sys.stdin)
assert d["active_animations"] == 0 and d["timer_armed"] is False
'
	ctl windows list | python3 -c '
import json,sys
assert json.load(sys.stdin)[0]["minimized"] is True
'
	ended=$(ctl effects status | python3 -c 'import json,sys; print(json.load(sys.stdin)["animations_started"])')
	[ "$ended" -gt "$started" ] || {
		echo "minimize fade did not increment animations_started" >&2
		exit 1
	}
	kill "$client_pid" 2>/dev/null || true
	wait "$client_pid" 2>/dev/null || true
	client_pid=
else
	echo "GTK 3 not available; minimize fade test skipped" >&2
fi

echo "=== shutdown cleanup ==="
kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=
if [ -e "$sock" ]; then
	echo "IPC socket was not removed on shutdown" >&2
	exit 1
fi
