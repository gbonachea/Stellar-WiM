#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Measure idle cost of two Wayland compositors under the same headless setup.
#
# Usage: t/benchmark.sh /path/to/labwc /path/to/stellar-wim [iterations]
#
# This is a measurement helper, not a pass/fail test. It reports wall-clock
# time until the Wayland socket appears, CPU time consumed while idle and
# resident memory, all read from /proc. No rendering workload is simulated.

set -eu

baseline_bin=$1
stellar_bin=$2
iterations=${3:-3}
case $iterations in
	''|*[!0-9]*|0) echo "iterations must be a positive integer" >&2; exit 1 ;;
esac
clk_tck=$(getconf CLK_TCK)

run_once() {
	run_label=$1
	run_binary=$2
	case "$run_binary" in
		/*) run_binary_abs=$run_binary ;;
		*) run_binary_abs=$PWD/$run_binary ;;
	esac

	run_dir=$(mktemp -d /tmp/stellar-bench.XXXXXX)
	mkdir -p "$run_dir/runtime" "$run_dir/config/labwc" \
		"$run_dir/config/stellar-wim" "$run_dir/data" "$run_dir/empty"
	chmod 700 "$run_dir/runtime"

	# Keep both configurations equivalent: an empty inherited config for
	# Labwc, and the same config plus the Stellar IPC block (on by default).
	printf '<labwc_config>\n</labwc_config>\n' >"$run_dir/config/labwc/rc.xml"
	cat >"$run_dir/config/stellar-wim/rc.xml" <<'EOF'
<labwc_config>
  <stellar-wim>
    <ipc enabled="true"/>
  </stellar-wim>
</labwc_config>
EOF

	run_env="XDG_CONFIG_HOME=$run_dir/config"
	run_env="$run_env XDG_CONFIG_DIRS=$run_dir/empty"
	run_env="$run_env XDG_DATA_HOME=$run_dir/data"
	run_env="$run_env XDG_RUNTIME_DIR=$run_dir/runtime"
	run_env="$run_env WAYLAND_DISPLAY=wayland-0"
	run_env="$run_env WLR_BACKENDS=headless"
	run_env="$run_env STELLAR_WIM_IPC_SOCKET=$run_dir/runtime/bench.sock"

	start_ms=$(python3 -c 'import time; print(int(time.monotonic() * 1000))')
	# shellcheck disable=SC2086
	env $run_env "$run_binary_abs" -d >"$run_dir/compositor.log" 2>&1 &
	run_pid=$!

	poll=0
	while [ ! -S "$run_dir/runtime/wayland-0" ]; do
		if ! kill -0 "$run_pid" 2>/dev/null; then
			echo "$run_label exited before its Wayland socket was ready" >&2
			cat "$run_dir/compositor.log" >&2
			rm -rf "$run_dir"
			exit 1
		fi
		poll=$((poll + 1))
		[ "$poll" -lt 100 ] || {
			echo "timed out waiting for $run_label" >&2
			rm -rf "$run_dir"
			exit 1
		}
		sleep 0.05
	done
	end_ms=$(python3 -c 'import time; print(int(time.monotonic() * 1000))')
	startup_ms=$((end_ms - start_ms))

	# Let startup work settle before sampling idle CPU.
	sleep 1
	ticks_before=$(awk '{print $14 + $15}' "/proc/$run_pid/stat")
	rss_kb=$(awk '/VmRSS/{print $2}' "/proc/$run_pid/status")
	sleep 3
	ticks_after=$(awk '{print $14 + $15}' "/proc/$run_pid/stat")

	kill -TERM "$run_pid" 2>/dev/null || true
	wait "$run_pid" 2>/dev/null || true
	rm -rf "$run_dir"

	cpu_ms=$(((ticks_after - ticks_before) * 1000 / clk_tck))
	printf '%s %s %s\n' "$startup_ms" "$cpu_ms" "$rss_kb"
}

. "$(dirname "$0")/benchmark-lib.sh"

measure() {
	measure_label=$1
	measure_binary=$2
	measure_out=$3
	: >"$measure_out"
	i=0
	while [ "$i" -lt "$iterations" ]; do
		printf '%s ' "$measure_label" >>"$measure_out"
		run_once "$measure_label" "$measure_binary" >>"$measure_out"
		i=$((i + 1))
	done
}

results=$(mktemp -d /tmp/stellar-bench-results.XXXXXX)
trap 'rm -rf "$results"' EXIT HUP INT TERM

measure baseline "$baseline_bin" "$results/base"
measure stellar "$stellar_bin" "$results/stellar"

base_startup=$(median "$results/base" 2)
base_cpu=$(median "$results/base" 3)
base_rss=$(median "$results/base" 4)
st_startup=$(median "$results/stellar" 2)
st_cpu=$(median "$results/stellar" 3)
st_rss=$(median "$results/stellar" 4)

echo "Headless idle comparison ($iterations runs each, median reported)"
echo
echo "| Compositor | Startup to Wayland socket (ms) | Idle CPU over 3 s (ms) | Idle RSS (KB) |"
echo "| --- | --- | --- | --- |"
echo "| Labwc 0.20.3 baseline | $base_startup | $base_cpu | $base_rss |"
echo "| Stellar WiM 0.8.0 (default config) | $st_startup | $st_cpu | $st_rss |"
echo
echo "Raw samples (label startup_ms cpu_ms rss_kb):"
cat "$results/base" "$results/stellar"
