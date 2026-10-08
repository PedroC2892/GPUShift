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
	# M3: progress is visible on the console while the screen is otherwise black.
	has "$UNIT" "StandardOutput=journal+console"
	has "$UNIT" "StandardError=journal+console"
fi

# The recovery guide must match the code (M1).
for doc in "$SRC/docs/RECOVERY.md" "$SRC/docs/RECOVERY.pt_PT.md"; do
	has "$doc" "/etc/modprobe.d/gpushift.conf"
	has "$doc" "/etc/udev/rules.d/50-gpushift.rules"
	has "$doc" "gpushift.reset=1"
	has "$doc" "reset --force"
	# Backups must be put back by hand before /var/lib/gpushift is deleted.
	has "$doc" "sudo cp /mnt/var/lib/gpushift/backup/gpushift.conf /mnt/etc/modprobe.d/"
	# Every MUX attribute GPUShift writes has a manual way back.
	for attr in $(grep -o '"/sys/[^"]*"' "$SRC/src/lib/mux.c" | tr -d '"' | grep -v dgpu_disable); do
		has "$doc" "$attr"
	done
done

# M4: limits that can surprise users are documented.
for doc in "$SRC/README.md" "$SRC/docs/RECOVERY.md"; do
	has "$doc" "without XDG autostart"
done
has "$SRC/docs/RECOVERY.pt_PT.md" "sem arranque automático XDG"
has "$SRC/README.md" "external GPU (eGPU)"

[ "$failures" -eq 0 ] || exit 1
