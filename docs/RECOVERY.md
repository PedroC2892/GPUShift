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
the previous mode on the **third boot** without confirmation: it puts the
previous configuration back, regenerates the initramfs and reboots once. So
if the screen stays black, reboot (hold the power button if needed) and let it
start two more times.

The reason is recorded in `/var/lib/gpushift/log` and in the journal
(`journalctl -u gpushift-boot-check`). The automatic reboot happens at most
once per change, so it can never loop.

This step needs systemd. On systems without systemd the service is not
installed and only steps 2 to 5 are available.

## 2. Text console (TTY)

1. Press **Ctrl+Alt+F3** (F2 to F6 also work on most systems).
2. Log in with your user name and password.
3. Run:

   ```sh
   sudo gpushift reset
   sudo reboot
   ```

## 3. Boot parameter `gpushift.reset=1`

This does not change your bootloader configuration; the parameter applies to
that one boot only. GPUShift sees it early in the boot, removes all of its
changes and reboots once into the original configuration.

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

**Without GPUShift**, do the same by hand:

```sh
sudo rm -f /mnt/etc/modprobe.d/gpushift.conf /mnt/etc/udev/rules.d/50-gpushift.rules
# Files that existed before GPUShift, if any:
ls /mnt/var/lib/gpushift/backup/
sudo rm -rf /mnt/var/lib/gpushift
```

In both cases, regenerate the initramfs from a chroot. Mount `/boot` (and
`/boot/efi`) first if they are separate partitions:

```sh
for d in dev proc sys run; do sudo mount --rbind /$d /mnt/$d; done
sudo chroot /mnt update-initramfs -u -k all        # Debian, Ubuntu
sudo chroot /mnt dracut --force --regenerate-all   # Fedora, openSUSE
sudo chroot /mnt mkinitcpio -P                     # Arch Linux
```

Then reboot without the USB stick.

### Laptops with a firmware MUX

If you had used Dedicated mode on an ASUS laptop, the MUX may still route the
panel to the dedicated GPU. Once the system boots, `sudo gpushift reset`
restores it. From a live USB running on the same laptop you can also run
`echo 1 | sudo tee /sys/devices/platform/asus-nb-wmi/gpu_mux_mode` and reboot.
