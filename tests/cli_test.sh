#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Black-box tests for the gpushift CLI against the fake sysfs fixtures.
# Usage: cli_test.sh <gpushift binary> <fixtures dir>
set -u
CLI=$1
FIXTURES=$2
failures=0

fail() {
	echo "FAIL: $*" >&2
	failures=$((failures + 1))
}

# expect <fixture> <expected exit code> <pattern> <args...>
expect() {
	fixture=$1 code=$2 pattern=$3
	shift 3
	out=$(GPUSHIFT_SYSFS_ROOT="$FIXTURES/$fixture" "$CLI" "$@" 2>&1)
	rc=$?
	[ "$rc" -eq "$code" ] || fail "$fixture '$*': exit $rc, expected $code"
	printf '%s\n' "$out" | grep -q -- "$pattern" || fail "$fixture '$*': no match for '$pattern'"
}

expect intel-nvidia 0 'Driver:         nvidia (version 580.82.09)' status
expect intel-nvidia 0 '"driver":"nvidia","driver_version":"580.82.09"' status --json
expect intel-nvidia 0 '"modes":\["integrated","hybrid"\]' modes --json
expect intel-nvidia 0 '\* hybrid' modes
expect asus-mux 0 '"mux":"asus-wmi"' modes --json
expect asus-mux 0 'dedicated' modes
expect conflict 0 '"switchable":"conflict"' --json modes
expect single-intel 0 'only one GPU' modes
expect single-intel 0 '"modes":\[\]' status --json
expect desktop-2gpu 0 'not a laptop' modes
expect dgpu-nodriver 0 'Driver:         none' status
expect intel-nvidia 2 'unknown command' frobnicate
expect intel-nvidia 2 'Usage' 
expect intel-nvidia 0 'gpushift [0-9]' --version

[ "$failures" -eq 0 ] || exit 1
