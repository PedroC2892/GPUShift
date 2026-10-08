# GPUShift

GPUShift is a GPU mode manager for Linux. It detects the GPUs in the system,
shows detailed information about them and switches between the GPU modes the
hardware and drivers actually support:

- **Integrated**: the dedicated GPU (dGPU) is powered off and removed from the
  PCI bus. Lowest power use.
- **Hybrid**: the integrated GPU (iGPU) drives the display and the dGPU is used
  on demand through PRIME render offload.
- **Dedicated**: the firmware MUX routes the display to the dGPU. Only offered
  on laptops with a supported MUX.

GPUShift only uses kernel interfaces (sysfs, procfs, modprobe.d, udev) and
polkit. It works the same on any desktop environment (KDE Plasma, GNOME, Xfce,
...) and on Wayland and X11. It ships a command line tool (`gpushift`) and a
Qt6 interface (`gpushift-gui`) that follows the system theme.

## Supported systems

Distributions: any modern distribution with systemd-udev or eudev, kmod and
polkit. These families are supported; Debian stable, Ubuntu LTS, Fedora and
Arch are built and tested in CI:

| Family           | Initramfs generator | Packages            |
|------------------|---------------------|---------------------|
| Debian / Ubuntu  | `update-initramfs`  | `.deb` (CPack)      |
| Fedora / RHEL    | `dracut`            | `.rpm` (CPack)      |
| Arch Linux       | `mkinitcpio`, `booster` | `PKGBUILD`      |
| openSUSE         | `dracut`            | `.rpm` (CPack)      |

Hardware:

- Laptops with exactly one iGPU and one dGPU: Intel or AMD iGPU with an NVIDIA
  (proprietary `nvidia`, `nvidia-open` or `nouveau`), AMD or Intel Arc dGPU.
- Firmware MUX backends: ASUS (`asus-wmi` and the newer `asus-armoury`
  firmware attributes) and Lenovo Legion (`legion_laptop` module from
  LenovoLegionLinux).
- Systems with one GPU and desktops with several GPUs get the information view
  only: there is nothing safe to switch there.

## Limitations

- Every change needs a reboot. GPUShift never unloads drivers or switches GPUs
  live.
- Only one iGPU plus one dGPU. External GPUs and machines with more GPUs are
  shown but not managed.
- In Integrated mode, outputs wired to the dGPU (often HDMI or USB-C on gaming
  laptops) stop working.
- Dedicated mode needs a MUX backend and a dGPU with a working driver; it is
  not offered while the dGPU is removed (switch to Hybrid first).
- GPUShift does not sign kernel modules. With Secure Boot enabled, the NVIDIA
  modules must already be signed by your distribution or by you.
- The kernel command line and the bootloader configuration are never touched.
- The Lenovo Legion backend has not been validated on real hardware yet.

## Dependencies

Build: a C17 and C++17 compiler (GCC or Clang), CMake 3.20 or newer,
pkg-config, libpci (pciutils), Qt 6 Base and Qt 6 LinguistTools (GUI only)
and polkit development files (only used to find the polkit actions directory).
Runtime: libpci, the `pci.ids` database, polkit with `pkexec` and Qt 6 (GUI).

| Distribution    | Packages |
|-----------------|----------|
| Debian / Ubuntu | `build-essential cmake pkg-config libpci-dev pci.ids qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libpolkit-gobject-1-dev`, runtime `pkexec` (or `policykit-1` on older releases) |
| Fedora / RHEL   | `gcc gcc-c++ cmake pkgconf-pkg-config pciutils-devel hwdata qt6-qtbase-devel qt6-qttools-devel polkit-devel` |
| Arch Linux      | `base-devel cmake pkgconf pciutils hwdata qt6-base qt6-tools polkit` |
| openSUSE        | `gcc gcc-c++ cmake pkg-config pciutils-devel hwdata qt6-base-devel qt6-tools-devel qt6-linguist-devel polkit-devel` |

`.github/scripts/install-deps.sh` installs all of them (plus Clang) on these
distributions.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options:

| Option | Default | Meaning |
|--------|---------|---------|
| `GPUSHIFT_BUILD_GUI` | `ON` | Build `gpushift-gui`. Use `OFF` on servers or systems without Qt. |
| `BUILD_TESTING` | `ON` | Build the unit and integration tests. |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Use `/usr` for packages. |
| `GPUSHIFT_POLKIT_ACTIONS_DIR` | from `pkg-config polkit-gobject-1`, else `/usr/share/polkit-1/actions` | Where the polkit policy goes. |
| `GPUSHIFT_MODPROBE_DIR` | `/etc/modprobe.d` | Where the runtime modprobe file is written. |
| `GPUSHIFT_UDEV_RULES_DIR` | `/etc/udev/rules.d` | Where the runtime udev rule is written. |

Debug builds (`-DCMAKE_BUILD_TYPE=Debug`) enable AddressSanitizer and
UndefinedBehaviorSanitizer. All builds use `-Wall -Wextra -Wpedantic -Werror`.

## Installing

```sh
sudo cmake --install build
```

This installs `gpushift` and `gpushift-gui` to `bin/`, `gpushift-helper` to
`libexec/`, the polkit policy, the desktop entry, the icon and the
translations. The polkit policy always goes to polkit's own directory
(normally `/usr/share/polkit-1/actions`), because polkit does not read
`/usr/local`.

Packages (build them on the distribution they are meant for):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cd build && cpack            # gpushift_<version>_<arch>.deb and gpushift-<version>-1.<arch>.rpm
```

For Arch Linux, use `packaging/arch/PKGBUILD` with `makepkg -si`.

### Why no Flatpak or Snap

GPUShift has to read the real `/sys` and `/proc`, write to `/etc/modprobe.d`
and `/etc/udev/rules.d`, change firmware attributes and run the host's
initramfs generator as root. A sandboxed package cannot do any of that without
punching holes that remove the point of the sandbox, so GPUShift is only
distributed as native packages.

## Using the CLI

```
gpushift status [--json]    System and GPU information, plus the mode summary
gpushift modes [--json]     Current mode, pending mode and available modes
gpushift set <mode> [--yes] Switch to integrated, hybrid or dedicated
gpushift reset [--yes]      Remove every change made by GPUShift
```

`set` and `reset` ask for confirmation (skip it with `--yes`; it is required
when standard input is not a terminal) and authenticate through polkit. When
run as root they call the helper directly, which is how to use them over SSH.

Example:

```
$ gpushift modes
Current mode:   hybrid
Available modes:
    integrated  dedicated GPU powered off and removed; lowest power use
  * hybrid      integrated GPU drives the display, dedicated GPU on demand (PRIME offload)
$ gpushift set integrated
Switching to integrated mode. The change takes effect after a reboot.
The dedicated GPU will be powered off; displays wired to it will not work.
Continue? [y/N] y
Done. Reboot to switch to integrated mode.
```

To run a program on the dGPU in Hybrid mode, use your desktop's "Launch using
dedicated graphics card" entry (provided by switcheroo-control), or set the
PRIME variables yourself: `__NV_PRIME_RENDER_OFFLOAD=1
__GLX_VENDOR_LIBRARY_NAME=nvidia` for NVIDIA, `DRI_PRIME=1` for Mesa drivers.

Exit codes (shared by `gpushift` and `gpushift-helper`):

| Code | Meaning |
|------|---------|
| 0  | Success |
| 1  | Unexpected error, or confirmation declined |
| 2  | Invalid arguments |
| 3  | Only one GPU: there are no modes to switch |
| 4  | Switching not supported (desktop or unsupported GPU combination) |
| 5  | Mode not available on this system |
| 6  | A conflicting GPU switching tool is active |
| 7  | No supported initramfs generator found |
| 8  | Could not write the configuration |
| 9  | Initramfs generation failed; changes were rolled back |
| 10 | Could not change the firmware MUX |
| 11 | Not root, and `pkexec` not found |
| 12 | polkit authentication cancelled or denied |

## What each mode changes

GPUShift owns two files and one state directory. Before it first writes to
`/etc`, any existing file at those paths is copied to
`/var/lib/gpushift/backup/`.

**Integrated**

- `/etc/modprobe.d/gpushift.conf`: `blacklist` and `alias <module> off` for
  every module that can drive the dGPU (`nvidia`, `nvidia_drm`,
  `nvidia_modeset`, `nvidia_uvm`, `nouveau`; `amdgpu`, `radeon`; `i915`, `xe`),
  except the module the iGPU uses.
- `/etc/udev/rules.d/50-gpushift.rules`: a rule matching every PCI function of
  the dGPU slot (GPU, HDMI audio, USB-C controller) that enables runtime power
  management and removes the device from the bus.
- On a MUX laptop, the MUX is set to the iGPU.

**Hybrid**

- NVIDIA dGPU: `options nvidia-drm modeset=1` and
  `options nvidia NVreg_DynamicPowerManagement=0x02` in the modprobe file, and
  a udev rule enabling runtime power management for the dGPU, so it sleeps
  when idle.
- AMD and Intel dGPUs: nothing; the GPUShift files are removed.
- On a MUX laptop, the MUX is set to the iGPU.

**Dedicated**

- The MUX is set to the dGPU (and `dgpu_disable` cleared on ASUS).
- NVIDIA: `options nvidia-drm modeset=1`. No udev rule.

After writing, the initramfs is regenerated so that early boot sees the same
module configuration. State is kept in `/var/lib/gpushift/state`
(`key=value`) and a pending change in `/run/gpushift/pending`.

## Reverting manually

`gpushift reset` undoes everything. If you cannot run it (for example, no
graphical session), do the same by hand from a text console (Ctrl+Alt+F3) or a
rescue boot (add `systemd.unit=multi-user.target` to the kernel command line
from the boot menu for that one boot):

```sh
sudo rm -f /etc/modprobe.d/gpushift.conf /etc/udev/rules.d/50-gpushift.rules
# Restore files that existed before GPUShift, if any were backed up:
ls /var/lib/gpushift/backup/
sudo cp /var/lib/gpushift/backup/gpushift.conf /etc/modprobe.d/        # only if present
sudo cp /var/lib/gpushift/backup/50-gpushift.rules /etc/udev/rules.d/  # only if present
sudo rm -rf /var/lib/gpushift
# ASUS MUX back to hybrid (only if you used Dedicated mode):
echo 1 | sudo tee /sys/devices/platform/asus-nb-wmi/gpu_mux_mode
# Regenerate the initramfs with your distribution's tool:
sudo update-initramfs -u -k all      # Debian, Ubuntu
sudo dracut --force --regenerate-all # Fedora, openSUSE
sudo mkinitcpio -P                   # Arch
sudo reboot
```

## Design decisions

These are the choices made where the requirements left room, always taking the
safest, most portable and simplest option.

- **One writer.** Only `gpushift-helper` changes the system. It accepts a
  closed argument list (`apply integrated|hybrid|dedicated`, `reset`), runs no
  shell (`fork`/`execve` with absolute paths and a fixed environment), detects
  and validates everything again instead of trusting its caller, and writes
  files atomically (temporary file, `fsync`, `rename`).
- **polkit policy.** The action `org.gpushift.helper` is `auth_admin_keep` for
  active local sessions and denied for inactive and remote ones. Remote
  administrators use `sudo gpushift ...`, which runs the helper directly.
- **Laptops only.** Switching is offered only when the SMBIOS chassis type is a
  portable one, or unknown with an internal panel present. Desktops with
  several GPUs get information only, as required.
- **Never without a display.** Integrated and Hybrid need an iGPU with a
  driver that drives the internal panel (or a MUX that can route it there).
  Dedicated needs a MUX and a dGPU with a bound driver. The iGPU's own driver
  is never blacklisted (important for AMD APU + AMD dGPU, which share
  `amdgpu`).
- **iGPU or dGPU.** NVIDIA is always dedicated; Intel is integrated except the
  Arc discrete device IDs; an AMD GPU is integrated when its PCI slot also
  holds AMD SoC functions (vendor `0x1022`: PSP, USB), which discrete cards
  never have.
- **Integrated mode removes the whole slot.** The udev rule matches every
  function of the dGPU slot and sets runtime PM to `auto` before removal, so
  audio and USB-C functions do not keep the card awake.
- **Removed dGPU stays visible.** In Integrated mode the dGPU is gone from
  sysfs; its address and IDs are kept in the state file so GPUShift still
  shows it and still offers the other modes.
- **Reboot pending.** The pending marker lives in `/run`, a tmpfs, so a reboot
  clears it without any boot hook. Switching back to the running mode before
  rebooting clears it too.
- **Initramfs.** The first generator found is used, in this order:
  `update-initramfs`, `mkinitcpio`, `dracut`, `booster` (through its packaged
  `/usr/lib/booster/regenerate_images` script). Only `/usr/sbin`, `/usr/bin`,
  `/sbin` and `/bin` are searched. If none is found nothing is written. If the
  generator fails, the previous files and MUX value are restored.
- **Paths.** Runtime files go to the administrator directories
  (`/etc/modprobe.d`, `/etc/udev/rules.d`), which override vendor
  directories; `pkg-config` only reports vendor directories (`udevdir`), so it
  is used for the polkit actions directory, and the runtime directories are
  CMake cache variables instead.
- **Conflicts.** A tool counts as active when its daemon runs (supergfxd,
  system76-power, optimus-manager-daemon, bumblebeed) or when the files it
  writes while in use exist (envycontrol's modprobe and udev files,
  optimus-manager's Xorg file, `/etc/prime-discrete` for nvidia-prime). Ubuntu
  systems where `prime-select` was used are therefore blocked; remove the
  `nvidia-prime` package (or that file) first. switcheroo-control only
  launches applications on the dGPU and is reported as compatible.
- **No wake-ups.** VRAM of NVIDIA cards is read through NVML (loaded with
  `dlopen`, optional) only when the card is already awake, because NVML wakes a
  suspended GPU.
- **Secure Boot** is read from the `SecureBoot` EFI variable in efivarfs:
  missing `/sys/firmware/efi` means legacy BIOS, a missing variable means the
  firmware has no Secure Boot.
- **Test roots.** `GPUSHIFT_SYSFS_ROOT` prefixes every read path (`/sys`,
  `/proc`, `/etc/os-release`), which also helps with bug reports from copied
  trees. `GPUSHIFT_ETC_ROOT` prefixes every path GPUShift writes or executes
  (`/etc`, `/var/lib`, `/run`, initramfs tools). The installed helper drops
  both variables; only the uninstalled `gpushift-helper-test` honours them,
  and it refuses to run without `GPUSHIFT_ETC_ROOT`.
- **Static library.** libgpushift is linked statically into the three
  programs and not installed; its API is not stable yet.
- **GUI.** The Mode section comes first, then system and per-GPU cards. No
  style is forced; Qt picks the platform theme (Breeze, Adwaita, ...).
  Messages from the library are mapped to translatable strings in the GUI;
  the CLI stays in English.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
