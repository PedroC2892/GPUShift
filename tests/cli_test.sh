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

# set/reset must never reach the helper here: an empty write prefix has no
# initramfs generator, so every path below stops before running it.
GPUSHIFT_ETC_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/gpushift-cli-test.XXXXXX")
export GPUSHIFT_ETC_ROOT
trap 'rm -rf "$GPUSHIFT_ETC_ROOT"' EXIT
expect single-intel 3 'only one GPU' set hybrid --yes
expect single-amd 3 'only one GPU' set integrated --yes
expect desktop-2gpu 4 'not a laptop' set integrated --yes
expect conflict 6 'conflicting tool active: supergfxctl' set integrated --yes
expect intel-nvidia 5 'not available' set dedicated --yes
expect intel-nvidia 2 'expected a mode' set turbo
expect intel-nvidia 2 'expected a mode' set
expect intel-nvidia 2 'unexpected argument' set hybrid integrated
expect intel-nvidia 0 'Already in hybrid mode' set hybrid
expect intel-nvidia 7 'No supported initramfs generator' set integrated --yes
expect intel-nvidia 1 'use --yes' reset </dev/null

expect intel-nvidia 0 '' confirm   # nothing to confirm: silent no-op

# Unconfirmed change after a reboot (no pending marker in /run).
mkdir -p "$GPUSHIFT_ETC_ROOT/var/lib/gpushift"
printf 'mode=integrated\ndgpu=0000:01:00.0\ndgpu_vendor=10de\ndgpu_device=25a2\npending=1\nboot_attempts=2\nprevious_mode=hybrid\n' \
	> "$GPUSHIFT_ETC_ROOT/var/lib/gpushift/state"
expect intel-nvidia 0 'Unconfirmed:    2 of 3 boots' modes
expect intel-nvidia 0 '"awaiting_confirmation":true,"unconfirmed_boots":2' modes --json
rm -rf "$GPUSHIFT_ETC_ROOT/var"

# The recovery summary is shown before anything is applied.
mkdir -p "$GPUSHIFT_ETC_ROOT/usr/sbin"
printf '#!/bin/sh\nexit 0\n' > "$GPUSHIFT_ETC_ROOT/usr/sbin/update-initramfs"
chmod 755 "$GPUSHIFT_ETC_ROOT/usr/sbin/update-initramfs"
expect intel-nvidia 1 'gpushift.reset=1' set integrated </dev/null
rm -rf "$GPUSHIFT_ETC_ROOT/usr"

# reset --root on a mounted system (live USB recovery).
offline=$GPUSHIFT_ETC_ROOT/mnt
mkdir -p "$offline/etc/modprobe.d" "$offline/etc/udev/rules.d" "$offline/var/lib/gpushift/backup/previous"
echo "options original=1" > "$offline/var/lib/gpushift/backup/gpushift.conf"
echo "blacklist nvidia" > "$offline/etc/modprobe.d/gpushift.conf"
echo "ATTR{remove}" > "$offline/etc/udev/rules.d/50-gpushift.rules"
echo "x" > "$offline/var/lib/gpushift/backup/previous/50-gpushift.rules"
printf 'mode=dedicated\nmux_backend=asus-wmi\nmux_orig=hybrid\n' > "$offline/var/lib/gpushift/state"
echo "log" > "$offline/var/lib/gpushift/log"
echo "help" > "$offline/var/lib/gpushift/RECOVERY.txt"
printf 'NAME="Fedora Linux"\nID=fedora\n' > "$offline/etc/os-release"
expect intel-nvidia 0 'chroot .*/mnt dracut --force --regenerate-all' reset --root "$offline"
[ "$(cat "$offline/etc/modprobe.d/gpushift.conf")" = "options original=1" ] || fail "backup not restored"
[ ! -e "$offline/etc/udev/rules.d/50-gpushift.rules" ] || fail "udev rule not removed"
[ ! -e "$offline/var/lib/gpushift" ] || fail "/var/lib/gpushift not removed"
printf 'ID=linuxmint\nID_LIKE="ubuntu debian"\n' > "$offline/etc/os-release"
expect intel-nvidia 0 'update-initramfs -u -k all' reset --root "$offline"
expect intel-nvidia 2 'absolute path' reset --root relative/dir
expect intel-nvidia 2 'without --root' reset --root /

[ "$failures" -eq 0 ] || exit 1
