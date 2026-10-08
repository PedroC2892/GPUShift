#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs the build dependencies (GCC, Clang, CMake, libpci, Qt6, polkit) on
# the distributions used by CI. Run as root.
set -eu
. /etc/os-release
case "$ID" in
debian | ubuntu)
	export DEBIAN_FRONTEND=noninteractive
	apt-get update
	apt-get install -y --no-install-recommends \
		build-essential clang libclang-rt-dev cmake pkg-config libpci-dev pci.ids \
		qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
		libpolkit-gobject-1-dev
	;;
fedora)
	dnf install -y gcc gcc-c++ libasan libubsan clang compiler-rt cmake pkgconf-pkg-config \
		pciutils-devel hwdata qt6-qtbase-devel qt6-qttools-devel polkit-devel
	;;
arch)
	pacman -Syu --noconfirm --needed base-devel clang compiler-rt cmake pkgconf pciutils \
		hwdata qt6-base qt6-tools polkit
	;;
opensuse*)
	zypper --non-interactive install gcc gcc-c++ clang cmake pkg-config pciutils-devel \
		qt6-base-devel qt6-tools-devel qt6-linguist-devel polkit-devel
	;;
*)
	echo "unsupported distribution: $ID" >&2
	exit 1
	;;
esac
