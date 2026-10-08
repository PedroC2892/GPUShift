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
mkdir -p /var/lib/gpushift
echo "blacklist nvidia" > /etc/modprobe.d/gpushift.conf
echo 'ACTION=="add"' > /etc/udev/rules.d/50-gpushift.rules
printf 'mode=integrated\ndgpu=0000:01:00.0\ndgpu_vendor=10de\ndgpu_device=25a2\n' > /var/lib/gpushift/state

if [ "$kind" = deb ]; then
	apt-get purge -y gpushift-gui gpushift
else
	dnf remove -y gpushift-gui gpushift
fi

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
