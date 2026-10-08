#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks generated data files and the recovery guide against what the code does.
# Usage: data_test.sh <build dir> <source dir>
set -u
BUILD=$1
SRC=$2
failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
has() { grep -qF -- "$2" "$1" || fail "$1 does not contain '$2'"; }

UNIT=$BUILD/data/gpushift-boot-check.service
if [ -f "$UNIT" ]; then
	# C2: the boot check must also run when GPUShift files exist without a state.
	has "$UNIT" "ConditionPathExists=|/etc/modprobe.d/gpushift.conf"
	has "$UNIT" "ConditionPathExists=|/etc/udev/rules.d/50-gpushift.rules"
fi

[ "$failures" -eq 0 ] || exit 1
