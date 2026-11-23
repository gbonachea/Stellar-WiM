#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Shared helpers for t/benchmark.sh, kept separate so they can be tested.

# median FILE COLUMN
# Print the median of whitespace-separated values in COLUMN. Works for both
# odd and even sample counts (even counts return the arithmetic mean of the
# two central values, which can be a decimal).
median() {
	median_file=$1
	median_column=$2
	awk -v c="$median_column" '{print $c}' "$median_file" |
		sort -n |
		awk '{values[NR] = $1} END {
			if (NR == 0) exit 1;
			if (NR % 2) print values[(NR + 1) / 2];
			else print (values[NR / 2] + values[NR / 2 + 1]) / 2;
		}'
}
