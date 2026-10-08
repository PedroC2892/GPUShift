#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Integration tests for gpushift-helper (test build) in temporary prefixes.
# Fake initramfs generators record their arguments in "<tool>.log".
# Usage: helper_test.sh <gpushift-helper-test> <gpushift> <fixtures dir> <gpushift-boot-check-test>
set -u
HELPER=$1
CLI=$2
FIXTURES=$3
BOOTCHECK=$4
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

# boot: simulate a reboot (/run is a tmpfs) and run the boot check.
boot() {
	rm -rf "$ROOT/run/gpushift"
	GPUSHIFT_SYSFS_ROOT="$SYS" GPUSHIFT_ETC_ROOT="$ROOT" "$BOOTCHECK" 2>"$WORK/stderr"
	rc=$?
}
reboots() { [ -f "$ROOT/run/gpushift-test-reboots" ] && wc -l < "$ROOT/run/gpushift-test-reboots" || echo 0; }

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

new_case "boot counter: revert on the third unconfirmed boot" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 0
has $STATE "pending=1"
has $STATE "boot_attempts=0"
has $STATE "previous_mode=hybrid"
has /var/lib/gpushift/RECOVERY.txt "gpushift.reset=1"
run confirm; expect_rc 13          # no reboot yet: nothing to judge
boot; expect_rc 0; has $STATE "boot_attempts=1"
boot; expect_rc 0; has $STATE "boot_attempts=2"
[ "$(reboots)" -eq 0 ] || fail "rebooted too early"
boot; expect_rc 0
has $STATE "mode=hybrid"
has $STATE "pending=0"
has $STATE "auto_reboot=1"
absent $MODPROBE; absent $UDEV
has /var/lib/gpushift/log "reverted to hybrid mode: no confirmation after 3 boots"
has $PENDING "to=hybrid"
[ "$(reboots)" -eq 1 ] || fail "expected exactly one reboot"
boot; expect_rc 0; boot; expect_rc 0
[ "$(reboots)" -eq 1 ] || fail "rebooted again after the revert"

new_case "confirmation stops the counter" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 0
boot; expect_rc 0; has $STATE "boot_attempts=1"
run confirm; expect_rc 0
has $STATE "pending=0"
has /var/lib/gpushift/log "mode change confirmed"
boot; boot; boot; boot
has $STATE "mode=integrated"
has $STATE "boot_attempts=0"
[ "$(reboots)" -eq 0 ] || fail "confirmed change was reverted"
run revert; expect_rc 13

new_case "revert from the session restores the previous files" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 0
boot; run confirm; expect_rc 0
run apply hybrid; expect_rc 0
has $STATE "previous_mode=integrated"
run apply integrated; expect_rc 0   # back to the running mode before rebooting
has $STATE "pending=0"
run apply hybrid; expect_rc 0
boot; expect_rc 0
run revert; expect_rc 0
has $STATE "mode=integrated"
has $MODPROBE "blacklist nvidia"
has $UDEV "ATTR{remove}"
has $PENDING "from=hybrid"
run reset; expect_rc 0
absent /var/lib/gpushift

new_case "failed revert never loops" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
run apply integrated; expect_rc 0
boot; boot
fake_tool /usr/sbin/update-initramfs 1
boot; expect_rc 9
has $STATE "auto_reboot=1"
has $STATE "pending=1"
has $MODPROBE "blacklist nvidia"   # rolled back to the applied mode
fake_tool /usr/sbin/update-initramfs 0
boot; expect_rc 0
grep -q "not rebooting again" "$WORK/stderr" || fail "missing loop guard message"
[ "$(reboots)" -eq 0 ] || fail "rebooted despite the failed revert"

new_case "gpushift.reset=1 on the kernel command line" intel-nvidia
fake_tool /usr/sbin/update-initramfs 0
echo "BOOT_IMAGE=/vmlinuz root=UUID=1234 ro quiet gpushift.reset=10" > "$SYS/proc/cmdline"
run apply integrated; expect_rc 0
boot; expect_rc 0; has $STATE "boot_attempts=1"   # =10 is not the flag
echo "BOOT_IMAGE=/vmlinuz root=UUID=1234 ro quiet gpushift.reset=1" > "$SYS/proc/cmdline"
boot; expect_rc 0
absent $MODPROBE; absent $UDEV; absent /var/lib/gpushift
[ "$(reboots)" -eq 1 ] || fail "expected one reboot after the reset"
boot; expect_rc 0   # parameter left in place: nothing to reset, no reboot loop
[ "$(reboots)" -eq 1 ] || fail "reboot loop with a permanent gpushift.reset=1"

new_case "boot check without GPUShift state" intel-nvidia
boot; expect_rc 0
absent /var/lib/gpushift
[ "$(reboots)" -eq 0 ] || fail "rebooted without state"

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
for args in "" "apply" "apply default" "apply Hybrid" "reset now" "confirm now" "boot-check" "apply hybrid extra" "--help"; do
	# shellcheck disable=SC2086
	run $args; expect_rc 2
done
absent $MODPROBE
GPUSHIFT_SYSFS_ROOT="$SYS" "$HELPER" reset 2>/dev/null
[ $? -eq 2 ] || { name="no prefix"; fail "test helper ran without GPUSHIFT_ETC_ROOT"; }
GPUSHIFT_SYSFS_ROOT="$SYS" "$BOOTCHECK" 2>/dev/null
[ $? -eq 2 ] || { name="no prefix"; fail "test boot check ran without GPUSHIFT_ETC_ROOT"; }

[ "$failures" -eq 0 ] || exit 1
echo "all helper tests passed ($n cases)"
