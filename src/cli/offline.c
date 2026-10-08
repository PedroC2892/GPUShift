/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * gpushift reset --root <dir>: offline recovery of a system mounted at <dir>,
 * for example from a live USB. It only removes GPUShift's files (restoring
 * backups) and prints the initramfs command for the distribution found there.
 */
#include "offline.h"

#include "../lib/internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int restore_or_remove(const char *root, const char *target)
{
	char path[PATH_MAX], backup[PATH_MAX], data[4096];
	const char *base = strrchr(target, '/') + 1;
	snprintf(path, sizeof(path), "%s%s", root, target);
	snprintf(backup, sizeof(backup), "%s" GS_BACKUP_DIR "/%s", root, base);
	if (gs_read_file(backup, data, sizeof(data)) == 0) {
		FILE *f = fopen(path, "w");
		if (!f || fputs(data, f) < 0 || fclose(f) != 0)
			return -1;
		printf("Restored %s from the backup\n", path);
		return 0;
	}
	if (unlink(path) == 0) {
		printf("Removed %s\n", path);
		return 0;
	}
	return errno == ENOENT ? 0 : -1;
}

static void print_initramfs_hint(const char *root)
{
	char path[PATH_MAX], buf[4096], ids[512] = " ";
	snprintf(path, sizeof(path), "%s/etc/os-release", root);
	if (gs_read_file(path, buf, sizeof(buf)) < 0) {
		snprintf(path, sizeof(path), "%s/usr/lib/os-release", root);
		if (gs_read_file(path, buf, sizeof(buf)) < 0)
			buf[0] = '\0';
	}
	/* Collect ID and ID_LIKE as " id1 id2 " for word matching. */
	for (char *save = NULL, *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
		if (strncmp(l, "ID=", 3) != 0 && strncmp(l, "ID_LIKE=", 8) != 0)
			continue;
		for (char *v = strchr(l, '=') + 1; *v; v++)
			if (*v != '"' && *v != '\'' && strlen(ids) + 2 < sizeof(ids))
				strncat(ids, v, 1);
		strncat(ids, " ", sizeof(ids) - strlen(ids) - 1);
	}
	static const struct { const char *id, *cmd; } tools[] = {
		{ " debian ", "update-initramfs -u -k all" }, { " ubuntu ", "update-initramfs -u -k all" },
		{ " arch ", "mkinitcpio -P" },
		{ " fedora ", "dracut --force --regenerate-all" }, { " rhel ", "dracut --force --regenerate-all" },
		{ " suse ", "dracut --force --regenerate-all" }, { " opensuse ", "dracut --force --regenerate-all" },
	};
	const char *cmd = NULL;
	for (size_t i = 0; !cmd && i < sizeof(tools) / sizeof(tools[0]); i++)
		if (strstr(ids, tools[i].id))
			cmd = tools[i].cmd;

	printf("\nNow regenerate the initramfs of that system from a chroot:\n"
	       "  for d in dev proc sys run; do sudo mount --rbind /$d %s/$d; done\n"
	       "  sudo chroot %s %s\n", root, root,
	       cmd ? cmd : "<update-initramfs -u -k all | dracut --force --regenerate-all | mkinitcpio -P>");
	if (!cmd)
		printf("(distribution not recognised: use the generator it ships)\n");
	printf("If /boot is a separate partition, mount it at %s/boot first.\n", root);
}

int gs_reset_offline(const char *root)
{
	struct stat st;
	if (root[0] != '/' || stat(root, &st) < 0 || !S_ISDIR(st.st_mode)) {
		fprintf(stderr, "gpushift: --root needs the absolute path of a mounted root directory\n");
		return GS_ERR_USAGE;
	}
	if (strcmp(root, "/") == 0) {
		fprintf(stderr, "gpushift: for the running system use 'gpushift reset' without --root\n");
		return GS_ERR_USAGE;
	}

	/* The state is read with the target as write root to learn the original MUX value. */
	struct gs_state state;
	setenv("GPUSHIFT_ETC_ROOT", root, 1);
	bool has_state = gs_state_load(&state) == 0;
	unsetenv("GPUSHIFT_ETC_ROOT");

	if (restore_or_remove(root, GS_MODPROBE_FILE) < 0 || restore_or_remove(root, GS_UDEV_FILE) < 0) {
		fprintf(stderr, "gpushift: could not change files below %s: %s\n", root, strerror(errno));
		return GS_ERR_IO;
	}
	static const char *const files[] = {
		GS_PREVIOUS_DIR "/gpushift.conf", GS_PREVIOUS_DIR "/50-gpushift.rules",
		GS_BACKUP_DIR "/gpushift.conf", GS_BACKUP_DIR "/50-gpushift.rules",
		GS_STATE_FILE, GS_LOG_FILE, GS_RECOVERY_FILE,
	};
	static const char *const dirs[] = { GS_PREVIOUS_DIR, GS_BACKUP_DIR,
					    "/var/lib/gpushift/initramfs-backup", "/var/lib/gpushift" };
	char path[PATH_MAX], img[PATH_MAX * 2];
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		snprintf(path, sizeof(path), "%s%s", root, files[i]);
		unlink(path);
	}
	/* Initramfs copies of an interrupted regeneration: the chroot step rebuilds the images. */
	snprintf(path, sizeof(path), "%s/var/lib/gpushift/initramfs-backup", root);
	DIR *d = opendir(path);
	struct dirent *e;
	while (d && (e = readdir(d)))
		if (e->d_name[0] != '.') {
			snprintf(img, sizeof(img), "%s/%s", path, e->d_name);
			unlink(img);
		}
	if (d)
		closedir(d);
	for (size_t i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
		rmdir(path);
	}
	printf("GPUShift files removed from %s.\n", root);
	if (has_state && state.mux_backend[0] && strcmp(state.mux_orig, "dedicated") != 0)
		printf("\nThe firmware MUX (%s) may still select the dedicated GPU. Boot the system and run\n"
		       "'sudo gpushift reset', or switch it in the firmware setup.\n", state.mux_backend);
	print_initramfs_hint(root);
	return GS_OK;
}
