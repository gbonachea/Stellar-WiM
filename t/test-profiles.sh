#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# End-to-end test for Stellar WiM runtime profiles and SIGHUP reload.
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

cleanup() {
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

sock="$tmp/runtime/stellar-profiles-test.sock"
rc="$tmp/config/stellar-wim/rc.xml"
cat > "$rc" <<'EOF'
<labwc_config>
  <stellar-wim>
    <ipc enabled="true"/>
    <profile>enhanced</profile>
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
	env $env_common timeout 5s "$ctl_bin_abs" "$@"
}

subscribe() {
	# shellcheck disable=SC2086
	env $env_common "$ctl_bin_abs" "$@"
}

check_flags() {
	expected=$1
	ctl version | python3 -c '
import json, sys
expected = sys.argv[1] == "enhanced"
d = json.load(sys.stdin)
assert d["profile"] == sys.argv[1]
assert d["features"]["tabs"] is expected
assert d["features"]["tiling"] is expected
' "$expected"
	ctl capabilities | python3 -c '
import json, sys
expected = 1 if sys.argv[1] == "enhanced" else 0
d = json.load(sys.stdin)
assert d["profiles"] == 1
assert d["tabs"] == expected and d["tiling"] == expected
' "$expected"
	ctl tabs status | python3 -c '
import json, sys
expected = sys.argv[1] == "enhanced"
d = json.load(sys.stdin)
assert d["enabled"] is expected
' "$expected"
	ctl tiling status | python3 -c '
import json, sys
expected = sys.argv[1] == "enhanced"
d = json.load(sys.stdin)
assert d["enabled"] is expected
' "$expected"
	ctl effects status | python3 -c '
import json, sys
expected = sys.argv[1] == "enhanced"
d = json.load(sys.stdin)
for field in ("animations", "opacity", "shadows", "enabled"):
    assert d[field] is expected, (field, d[field], expected)
assert d["blur"] is False
' "$expected"
}

echo "=== startup profile from rc.xml ==="
ctl profile get | python3 -c 'import json,sys; assert json.load(sys.stdin)["profile"] == "enhanced"'
check_flags enhanced

echo "=== runtime switch to minimal ==="
ctl profile set minimal | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["profile"]=="minimal" and d["ok"] is True'
check_flags minimal
ctl profile get | python3 -c 'import json,sys; assert json.load(sys.stdin)["profile"] == "minimal"'

echo "=== runtime switch back to enhanced ==="
ctl profile set enhanced >/dev/null
check_flags enhanced

echo "=== invalid profile requests ==="
if ctl profile set bogus >"$tmp/bogus.out" 2>"$tmp/bogus.err"; then
	echo "profile set bogus unexpectedly succeeded" >&2
	exit 1
fi
grep -q 'expected minimal or enhanced' "$tmp/bogus.err"
if ctl profile set custom >"$tmp/custom.out" 2>"$tmp/custom.err"; then
	echo "profile set custom unexpectedly succeeded" >&2
	exit 1
fi
grep -q 'expected minimal or enhanced' "$tmp/custom.err"
if ctl profile get extra >"$tmp/extra.out" 2>"$tmp/extra.err"; then
	echo "profile get with an argument unexpectedly succeeded" >&2
	exit 1
fi
grep -q 'unknown profile command' "$tmp/extra.err"

echo "=== profile event ==="
subscribe events subscribe >"$tmp/events.out" 2>&1 &
subscriber_pid=$!
i=0
while ! grep -q 'subscribed' "$tmp/events.out" 2>/dev/null; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "timed out waiting for event subscriber" >&2
		cat "$tmp/events.out" >&2
		exit 1
	}
	sleep 0.1
done
ctl profile set minimal >/dev/null
i=0
while ! grep -q '"type":"profile"' "$tmp/events.out" 2>/dev/null; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "timed out waiting for profile event" >&2
		cat "$tmp/events.out" >&2
		exit 1
	}
	sleep 0.1
done
grep -q '"event":"switch"' "$tmp/events.out"
grep -q '"profile":"minimal"' "$tmp/events.out"
kill "$subscriber_pid" 2>/dev/null || true
wait "$subscriber_pid" 2>/dev/null || true
subscriber_pid=

echo "=== SIGHUP re-applies the configured profile ==="
cat > "$rc" <<'EOF'
<labwc_config>
  <stellar-wim>
    <ipc enabled="true"/>
    <profile>minimal</profile>
  </stellar-wim>
</labwc_config>
EOF
kill -HUP "$comp_pid"
i=0
while [ "$i" -lt 50 ]; do
	if ctl profile get 2>/dev/null |
		python3 -c 'import json,sys; sys.exit(0 if json.load(sys.stdin)["profile"] == "minimal" else 1)'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
ctl profile get | python3 -c 'import json,sys; assert json.load(sys.stdin)["profile"] == "minimal"'
check_flags minimal

echo "=== shutdown cleanup ==="
kill -TERM "$comp_pid"
wait "$comp_pid" || true
comp_pid=
if [ -e "$sock" ]; then
	echo "IPC socket was not removed on shutdown" >&2
	exit 1
fi
