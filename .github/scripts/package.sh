#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the .deb or .rpm packages on the current distribution, installs them,
# checks that gpushift runs, removes them and checks nothing is left behind.
# Usage: package.sh deb|rpm   (run as root in a throwaway container)
set -eu
kind=$1

cmake -S . -B build-pkg -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF
cmake --build build-pkg --parallel
(cd build-pkg && cpack -G "$(echo "$kind" | tr a-z A-Z)")
mkdir -p packages
mv build-pkg/*."$kind" packages/
ls -l packages

if [ "$kind" = deb ]; then
	export DEBIAN_FRONTEND=noninteractive
	apt-get install -y initramfs-tools
	apt-get install -y ./packages/gpushift_*.deb ./packages/gpushift-gui_*.deb
else
	dnf install -y dracut
	dnf install -y ./packages/gpushift-*.rpm
fi

gpushift --version
gpushift status
gpushift modes --json
test -e /etc/systemd/system/multi-user.target.wants/gpushift-boot-check.service

# Pretend a mode was applied: removal must undo it.
fake_mode() {
	mkdir -p /var/lib/gpushift
	echo "blacklist nvidia" > /etc/modprobe.d/gpushift.conf
	echo 'ACTION=="add"' > /etc/udev/rules.d/50-gpushift.rules
	printf 'mode=integrated\ndgpu=0000:01:00.0\ndgpu_vendor=10de\ndgpu_device=25a2\n' > /var/lib/gpushift/state
}
remove() {
	if [ "$kind" = deb ]; then apt-get "$1" -y gpushift-gui gpushift; else dnf remove -y gpushift-gui gpushift; fi
}
reinstall() {
	if [ "$kind" = deb ]; then
		apt-get install -y ./packages/gpushift_*.deb ./packages/gpushift-gui_*.deb
	else
		dnf install -y ./packages/gpushift-*.rpm
	fi
}
installed() {
	if [ "$kind" = deb ]; then dpkg -s gpushift 2>/dev/null | grep -q "Status: install ok installed"; else rpm -q gpushift >/dev/null; fi
}
generator=$(command -v update-initramfs || command -v dracut)

# A1: when nothing can undo the changes, removal must stop instead of leaving them behind.
fake_mode
rm /etc/udev/rules.d/50-gpushift.rules
mkdir /etc/udev/rules.d/50-gpushift.rules   # cannot be removed as a file
if remove remove; then
	echo "removal went on although the changes could not be undone"
	exit 1
fi
installed || { echo "package removed although the reset failed"; exit 1; }
rmdir /etc/udev/rules.d/50-gpushift.rules
reinstall   # gpushift-gui may already be gone

# Normal removal: the reset undoes the changes.
fake_mode
remove remove
test ! -e /etc/modprobe.d/gpushift.conf
reinstall

# A1: a failing initramfs generator must not leave the blacklist behind.
fake_mode
mv "$generator" "$generator.real"
printf '#!/bin/sh\nexit 1\n' > "$generator"
chmod 755 "$generator"
remove purge
mv "$generator.real" "$generator"

left=0
for f in /etc/modprobe.d/gpushift.conf /etc/udev/rules.d/50-gpushift.rules /var/lib/gpushift \
	/etc/xdg/autostart/gpushift-confirm.desktop /usr/bin/gpushift /usr/bin/gpushift-gui \
	/usr/libexec/gpushift-helper /usr/libexec/gpushift-boot-check \
	/usr/lib/systemd/system/gpushift-boot-check.service \
	/etc/systemd/system/multi-user.target.wants/gpushift-boot-check.service; do
	if [ -e "$f" ] || [ -L "$f" ]; then
		echo "left behind: $f"
		left=1
	fi
done
exit $left
