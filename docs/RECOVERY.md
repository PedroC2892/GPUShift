# Recovering from a black screen

Every GPUShift mode change needs a reboot. If the computer comes back with a
black screen, or no graphical login, use these steps **in order**. Each one is
enough on its own; move to the next only if the previous one is not possible.

Portuguese version: [RECOVERY.pt_PT.md](RECOVERY.pt_PT.md).

## 1. Wait and reboot (automatic)

After a mode change GPUShift waits for confirmation. Logging in to a graphical
session confirms it: a small dialog asks *Keep* or *Revert*, and reverts by
itself after 30 seconds without an answer. Without the GUI, reaching the
session is enough.

If the change is never confirmed, the `gpushift-boot-check` service restores
the previous mode from the **third boot** without confirmation on: it puts the
previous configuration and firmware MUX setting back, regenerates the
initramfs and reboots once. So if the screen stays black, reboot (hold the
power button if needed) and let it start two more times. While it works, the
text console shows "gpushift: restoring the previous GPU mode, do not power
off"; wait for it to finish (it can take a few minutes).

If the revert itself fails (for example `/boot` is full), nothing is changed
and it is tried again on every following boot; from the fifth unconfirmed boot
on, the previous configuration and MUX setting are restored even if the
initramfs cannot be rebuilt. It never reboots in a loop.
Every regeneration keeps a copy of the existing initramfs images and puts them
back if the new ones are missing, empty or unreadable.

The reason is recorded in `/var/lib/gpushift/log` and in the journal
(`journalctl -u gpushift-boot-check`).

This step needs systemd. On systems without systemd the service is not
installed and only steps 2 to 5 are available. In sessions
without XDG autostart (i3, sway, other bare window managers, text-only use) run
`gpushift confirm` yourself after a change, or it is reverted on the third
boot.

## 2. Text console (TTY)

1. Press **Ctrl+Alt+F3** (F2 to F6 also work on most systems).
2. Log in with your user name and password.
3. Run:

   ```sh
   sudo gpushift reset
   sudo reboot
   ```

If `reset` reports that the initramfs could not be regenerated, use the
emergency reset, which removes GPUShift's files and restores the firmware MUX
anyway, then rebuild the initramfs once the problem (usually a full `/boot`)
is fixed:

```sh
sudo gpushift reset --force
sudo reboot
```

## 3. Boot parameter `gpushift.reset=1`

This does not change your bootloader configuration; the parameter applies to
that one boot only. GPUShift sees it early in the boot, removes all of its
changes, restores the firmware MUX and reboots once into the original
configuration. It works even if the GPUShift state is missing or the
initramfs cannot be rebuilt, and leaving the parameter in place does not cause
a reboot loop.

**GRUB** (Debian, Ubuntu, Fedora, openSUSE, most Arch installs):

1. Show the menu: hold **Shift** while booting (BIOS) or press **Esc**
   repeatedly (UEFI).
2. Select the normal entry and press **e**.
3. Find the line that starts with `linux` and add ` gpushift.reset=1` at
   the end of it.
4. Boot with **Ctrl+X** or **F10**.

**systemd-boot**:

1. Show the menu: hold **Space** while booting.
2. Select the normal entry and press **e**.
3. Add ` gpushift.reset=1` at the end of the options line.
4. Press **Enter** to boot.

## 4. Text mode boot

Boot without the graphical interface, then reset:

1. Edit the boot entry as in step 3, but add
   ` systemd.unit=multi-user.target` instead.
2. Log in on the text console and run:

   ```sh
   sudo gpushift reset
   sudo reboot
   ```

## 5. Live USB

Boot any Linux live USB and open a terminal. Find your root partition with
`lsblk -f` and mount it (replace `/dev/nvme0n1p2` with yours; for Btrfs
installs add `-o subvol=@` or the subvolume your distribution uses):

```sh
sudo mount /dev/nvme0n1p2 /mnt
```

**With GPUShift on the live system** (for example installed from the .deb):

```sh
sudo gpushift reset --root /mnt
```

It removes GPUShift's files from `/mnt`, restores backed-up files and prints
the initramfs command for the installed distribution.

**Without GPUShift**, do the same by hand. First put back any file that
existed before GPUShift (only if the backup exists), then delete GPUShift's
files:

```sh
ls /mnt/var/lib/gpushift/backup/
sudo cp /mnt/var/lib/gpushift/backup/gpushift.conf /mnt/etc/modprobe.d/        # only if listed
sudo cp /mnt/var/lib/gpushift/backup/50-gpushift.rules /mnt/etc/udev/rules.d/  # only if listed
# Delete whichever of the two was NOT restored above:
sudo rm -f /mnt/etc/modprobe.d/gpushift.conf /mnt/etc/udev/rules.d/50-gpushift.rules
sudo rm -rf /mnt/var/lib/gpushift
```

If `/mnt/var/lib/gpushift/initramfs-backup/` exists, a regeneration was
interrupted; the regeneration below replaces those images anyway.

In both cases, regenerate the initramfs from a chroot. Mount `/boot` (and
`/boot/efi`) first if they are separate partitions:

```sh
for d in dev proc sys run; do sudo mount --rbind /$d /mnt/$d; done
sudo chroot /mnt update-initramfs -u -k all        # Debian, Ubuntu
sudo chroot /mnt dracut --force --regenerate-all   # Fedora, openSUSE
sudo chroot /mnt mkinitcpio -P                     # Arch Linux
```

Then reboot without the USB stick.

## Laptops with a firmware MUX

Dedicated mode switches a firmware setting that survives reinstalls. Steps 1
to 3 restore it automatically (`gpushift.reset=1` included). The firmware
setup (BIOS) usually still shows a picture even when Linux does not, because
it uses the firmware's own graphics.

To switch the MUX back by hand, from the installed system (text console) or
from a live USB running on the same laptop, write the "hybrid" value and
reboot:

| Laptop | Attribute | Hybrid value |
|--------|-----------|--------------|
| ASUS, Linux 6.17 and newer | `/sys/class/firmware-attributes/asus-armoury/attributes/gpu_mux_mode/current_value` | `1` |
| ASUS, older kernels | `/sys/devices/platform/asus-nb-wmi/gpu_mux_mode` | `1` |
| Lenovo Legion (legion_laptop module) | `/sys/bus/platform/drivers/legion/PNP0C09:00/gsync` | `0` |

```sh
echo 1 | sudo tee /sys/devices/platform/asus-nb-wmi/gpu_mux_mode   # example: ASUS
sudo reboot
```

Without Linux at all, open the firmware setup (usually F2 or Del while
booting) and set the GPU mode back to hybrid / "Optimus" / "Dynamic graphics"
(the name depends on the vendor).
