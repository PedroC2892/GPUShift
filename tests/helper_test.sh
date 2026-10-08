#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Integration tests for gpushift-helper (test build) in temporary prefixes.
# Fake initramfs generators record their arguments in "<tool>.log".
# Usage: helper_test.sh <gpushift-helper-test> <gpushift> <fixtures dir>
set -u
HELPER=$1
CLI=$2
FIXTURES=$3
WORK=$(mktemp -d "${TMPDIR:-/tmp}/gpushift-helper-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
failures=0
n=0

fail() {
	echo "FAIL [$name]: $*" >&2
	failures=$((failures + 1))
}

# new_case <name> <fixture>: fresh write prefix and a private copy of the sysfs tree.
new_case() {
	name=$1
	n=$((n + 1))
	ROOT="$WORK/$n/root"
	SYS="$WORK/$n/sys"
	mkdir -p "$ROOT" "$SYS"
	cp -R "$FIXTURES/$2/." "$SYS/"
}

# fake_tool <absolute path> <exit code>
fake_tool() {
	mkdir -p "$ROOT$(dirname "$1")"
	printf '#!/bin/sh\necho "$*" > "$0.log"\nexit %s\n' "$2" > "$ROOT$1"
	chmod 755 "$ROOT$1"
}

run() {
	GPUSHIFT_SYSFS_ROOT="$SYS" GPUSHIFT_ETC_ROOT="$ROOT" "$HELPER" "$@" 2>"$WORK/stderr"
	rc=$?
}

expect_rc() { [ "$rc" -eq "$1" ] || fail "exit code $rc, expected $1 ($(cat "$WORK/stderr"))"; }
has() { grep -q -- "$2" "$ROOT$1" 2>/dev/null || fail "$1 does not contain '$2'"; }
absent() { [ ! -e "$ROOT$1" ] || fail "$1 should not exist"; }

MODPROBE=/etc/modprobe.d/gpushift.conf
UDEV=/etc/udev/rules.d/50-gpushift.rules
STATE=/var/lib/gpushift/state
PENDING=/run/gpushift/pending

new_case "integrated, hybrid, reset (update-initramfs)" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 0
has $MODPROBE "blacklist nvidia"
has $UDEV 'KERNEL=="0000:01:00.\*"'
has $STATE "mode=integrated"
has $STATE "dgpu=0000:01:00.0"
has $PENDING "from=hybrid"
has $PENDING "to=integrated"
has /usr/sbin/update-initramfs.log "-u -k all"
[ "$(stat -c %a "$ROOT$MODPROBE")" = 644 ] || fail "modprobe file is not 0644"
GPUSHIFT_SYSFS_ROOT="$SYS" GPUSHIFT_ETC_ROOT="$ROOT" "$CLI" modes | grep -q "Pending mode:   integrated" ||
	fail "CLI does not report the pending mode"
run apply hybrid; expect_rc 0
has $MODPROBE "NVreg_DynamicPowerManagement=0x02"
has $UDEV 'ACTION=="bind"'
has $STATE "mode=hybrid"
absent $PENDING   # back to the mode that is still running
run reset; expect_rc 0
absent $MODPROBE; absent $UDEV; absent $STATE; absent /var/lib/gpushift
has $PENDING "to=default"
run reset; expect_rc 0
grep -q "nothing to reset" "$WORK/stderr" || fail "second reset should be a no-op"

new_case "backup and restore of pre-existing files" intel-nvidia
fake_tool /usr/bin/update-initramfs 0
mkdir -p "$ROOT/etc/modprobe.d"
echo "options original=1" > "$ROOT$MODPROBE"
run apply integrated; expect_rc 0
has /var/lib/gpushift/backup/gpushift.conf "options original=1"
run apply hybrid; expect_rc 0
has /var/lib/gpushift/backup/gpushift.conf "options original=1"
run reset; expect_rc 0
has $MODPROBE "options original=1"
absent /var/lib/gpushift/backup

for tool in dracut:/usr/bin/dracut:"--force --regenerate-all" \
	    mkinitcpio:/usr/bin/mkinitcpio:"-P" \
	    booster:/usr/lib/booster/regenerate_images:""; do
	tname=${tool%%:*}; rest=${tool#*:}; log=${rest%%:*}; args=${rest#*:}
	new_case "$tname" amdapu-amd
	fake_tool "/usr/bin/$tname" 0
	[ "$tname" = booster ] && fake_tool /usr/lib/booster/regenerate_images 0
	run apply integrated; expect_rc 0
	[ -f "$ROOT$log.log" ] || fail "$log was not run"
	[ "$(cat "$ROOT$log.log" 2>/dev/null)" = "$args" ] || fail "wrong arguments for $tname"
	has $MODPROBE "blacklist radeon"
done

new_case "update-initramfs wins over dracut" intel-nouveau
fake_tool /usr/sbin/update-initramfs 0
fake_tool /usr/bin/dracut 0
run apply integrated; expect_rc 0
[ -f "$ROOT/usr/sbin/update-initramfs.log" ] && [ ! -f "$ROOT/usr/bin/dracut.log" ] ||
	fail "wrong generator chosen"

new_case "no initramfs generator" intel-nvidia
fake_tool /usr/local/bin/dracut 0   # not a searched directory
run apply integrated; expect_rc 7
absent $MODPROBE; absent $STATE

new_case "failing generator rolls back" intel-nvidia
fake_tool /usr/sbin/update-initramfs 1
run apply integrated; expect_rc 9
absent $MODPROBE; absent $UDEV; absent $STATE; absent $PENDING

new_case "ASUS MUX dedicated and reset" asus-mux
fake_tool /usr/sbin/update-initramfs 0
run apply dedicated; expect_rc 0
[ "$(cat "$SYS/sys/devices/platform/asus-nb-wmi/gpu_mux_mode")" = 0 ] || fail "MUX not set to dGPU"
has $STATE "mux_backend=asus-wmi"
has $STATE "mux_orig=hybrid"
has $MODPROBE "modeset=1"
absent $UDEV
run reset; expect_rc 0
[ "$(cat "$SYS/sys/devices/platform/asus-nb-wmi/gpu_mux_mode")" = 1 ] || fail "MUX not restored"

new_case "integrated mode after reboot: dGPU gone from the bus" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 0
rm -rf "$SYS/sys/bus/pci/devices/0000:01:00.0" "$SYS/sys/bus/pci/devices/0000:01:00.1" "$ROOT$PENDING"
out=$(GPUSHIFT_SYSFS_ROOT="$SYS" GPUSHIFT_ETC_ROOT="$ROOT" "$CLI" status)
echo "$out" | grep -q "removed from the PCI bus by GPUShift" || fail "removed dGPU not shown"
echo "$out" | grep -q "\* integrated" || fail "current mode is not integrated"
run apply dedicated; expect_rc 5
run apply hybrid; expect_rc 0
has $UDEV 'KERNEL=="0000:01:00.0"'
has $PENDING "from=integrated"

new_case "refusals" single-intel
fake_tool /usr/sbin/update-initramfs 0
run apply hybrid; expect_rc 3
new_case "desktop" desktop-2gpu
run apply integrated; expect_rc 4
new_case "conflict" conflict
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 6
absent $MODPROBE
new_case "unavailable mode" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply dedicated; expect_rc 5
new_case "argument list is closed" intel-nvidia
for args in "" "apply" "apply default" "apply Hybrid" "reset now" "apply hybrid extra" "--help"; do
	# shellcheck disable=SC2086
	run $args; expect_rc 2
done
absent $MODPROBE
GPUSHIFT_SYSFS_ROOT="$SYS" "$HELPER" reset 2>/dev/null
[ $? -eq 2 ] || { name="no prefix"; fail "test helper ran without GPUSHIFT_ETC_ROOT"; }

[ "$failures" -eq 0 ] || exit 1
echo "all helper tests passed ($n cases)"
