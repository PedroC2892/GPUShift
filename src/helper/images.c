/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Initramfs regeneration with a safety net. Generators differ in how they
 * write images (mkinitcpio writes in place when /boot is short of space) and
 * all of them stop half-way through the kernel list on an error, so GPUShift
 * does not rely on them: every image is copied aside first, every image is
 * validated afterwards, and on any failure all images are put back.
 */
#include "ops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MAX_IMAGES 64
#define BACKUP_DIR "/var/lib/gpushift/initramfs-backup"
#define MARGIN (16ULL << 20)

struct image {
	char path[PATH_MAX];
	off_t size;
	mode_t mode;
};

static struct image before[MAX_IMAGES], after[MAX_IMAGES];

static bool has_suffix(const char *s, const char *suffix)
{
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && strcmp(s + a - b, suffix) == 0;
}

static bool image_name(const char *n)
{
	static const char *const prefixes[] = { "initrd.img-", "initrd-", "initramfs-", "booster-" };
	static const char *const skip[] = { ".new", ".tmp", ".bak", ".old", ".dpkg-bak" };
	bool match = false;
	for (size_t i = 0; i < 4; i++)
		match |= strncmp(n, prefixes[i], strlen(prefixes[i])) == 0;
	for (size_t i = 0; match && i < 5; i++)
		match = !has_suffix(n, skip[i]);
	return match;
}

static void add_image(struct image *imgs, size_t *n, const char *path)
{
	struct stat st;
	if (*n < MAX_IMAGES && lstat(path, &st) == 0 && S_ISREG(st.st_mode)) {
		gs_strlcpy(imgs[*n].path, path, sizeof(imgs[0].path));
		imgs[*n].size = st.st_size;
		imgs[*n].mode = st.st_mode & 07777;
		(*n)++;
	}
}

/* /boot/{initrd.img-*,initrd-*,initramfs-*,booster-*} and <ESP>/<entry>/<version>/initrd. */
static size_t collect(struct image *imgs)
{
	static const char *const esps[] = { "/boot", "/boot/efi", "/efi" };
	char dir[PATH_MAX], sub[PATH_MAX * 2], path[PATH_MAX * 3];
	size_t n = 0;
	struct dirent *e, *e2;

	DIR *d = gs_wpath(dir, sizeof(dir), "/boot") == 0 ? opendir(dir) : NULL;
	while (d && (e = readdir(d))) {
		if (!image_name(e->d_name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		add_image(imgs, &n, path);
	}
	if (d)
		closedir(d);
	for (size_t i = 0; i < 3; i++) {
		if (gs_wpath(dir, sizeof(dir), "%s", esps[i]) < 0 || !(d = opendir(dir)))
			continue;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.')
				continue;
			snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name);
			DIR *d2 = opendir(sub);
			while (d2 && (e2 = readdir(d2))) {
				if (e2->d_name[0] == '.')
					continue;
				snprintf(path, sizeof(path), "%s/%s/initrd", sub, e2->d_name);
				add_image(imgs, &n, path);
			}
			if (d2)
				closedir(d2);
		}
		closedir(d);
	}
	return n;
}

static unsigned long long free_bytes(const char *path)
{
#ifdef GPUSHIFT_TEST_HOOKS
	const char *fake = getenv("GPUSHIFT_TEST_FREE_BYTES");
	if (fake)
		return strtoull(fake, NULL, 10);
#endif
	/* The directory may not exist yet: measure the filesystem of its nearest parent. */
	char dir[PATH_MAX];
	struct statvfs v;
	gs_strlcpy(dir, path, sizeof(dir));
	while (statvfs(dir, &v) < 0) {
		char *slash = strrchr(dir, '/');
		if (errno != ENOENT || !slash)
			return 0;
		if (slash == dir)
			slash[1] = '\0';
		else
			*slash = '\0';
	}
	return (unsigned long long)v.f_bavail * v.f_frsize;
}

static int copy_file(const char *from, const char *to, mode_t mode)
{
	int in = open(from, O_RDONLY | O_CLOEXEC);
	if (in < 0)
		return -1;
	int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
	if (out < 0) {
		close(in);
		return -1;
	}
	char buf[65536];
	ssize_t r = 0;
	bool ok = fchmod(out, mode) == 0;
	while (ok && (r = read(in, buf, sizeof(buf))) != 0) {
		if (r < 0 && errno == EINTR)
			continue;
		ok = r > 0;
		for (ssize_t off = 0; ok && off < r;) {
			ssize_t w = write(out, buf + off, (size_t)(r - off));
			if (w < 0 && errno == EINTR)
				continue;
			ok = w > 0;
			off += ok ? w : 0;
		}
	}
	ok = ok && fsync(out) == 0;
	ok = (close(out) == 0) && ok;
	close(in);
	return ok ? 0 : -1;
}

/* Puts an image back: next to it and renamed when possible, in place when /boot is full. */
static int restore_image(const char *backup, const struct image *img)
{
	char tmp[PATH_MAX + 32];
	int len = snprintf(tmp, sizeof(tmp), "%s.gpushift-restore", img->path);
	if (len > 0 && (size_t)len < sizeof(tmp) && copy_file(backup, tmp, img->mode) == 0 &&
	    rename(tmp, img->path) == 0)
		return 0;
	unlink(tmp);
	unlink(img->path); /* frees the space a broken image takes */
	return copy_file(backup, img->path, img->mode);
}

/* Uncompressed cpio or one of the compressors the generators use. */
static bool valid_magic(const char *path)
{
	static const struct { const char *bytes; size_t len; } magics[] = {
		{ "070701", 6 }, { "070702", 6 }, { "070707", 6 },  /* cpio (also early microcode) */
		{ "\x1f\x8b", 2 },                                   /* gzip */
		{ "\x28\xb5\x2f\xfd", 4 },                           /* zstd */
		{ "\xfd" "7zXZ", 5 },                                /* xz */
		{ "\x04\x22\x4d\x18", 4 }, { "\x02\x21\x4c\x18", 4 }, /* lz4 frame, legacy */
		{ "BZh", 3 },                                        /* bzip2 */
		{ "\x5d\x00\x00", 3 },                               /* lzma */
		{ "\x89LZO", 4 },                                    /* lzop */
	};
	unsigned char head[8];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	ssize_t n = read(fd, head, sizeof(head));
	close(fd);
	for (size_t i = 0; i < sizeof(magics) / sizeof(magics[0]); i++)
		if (n >= (ssize_t)magics[i].len && memcmp(head, magics[i].bytes, magics[i].len) == 0)
			return true;
	return false;
}

static bool image_ok(const char *path, const char *lister, const char *lister_arg)
{
	struct stat st;
	if (stat(path, &st) < 0 || st.st_size == 0 || !valid_magic(path)) {
		fprintf(stderr, "gpushift: %s is missing, empty or not an initramfs\n", path);
		return false;
	}
	if (!lister)
		return true;
	static char *const envp[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL=C", NULL };
	char *argv[4] = { (char *)lister, NULL, NULL, NULL };
	argv[lister_arg ? 2 : 1] = (char *)path;
	if (lister_arg)
		argv[1] = (char *)lister_arg;
	if (gs_spawn(lister, argv, envp, true) != 0) {
		fprintf(stderr, "gpushift: %s cannot read %s\n", lister, path);
		return false;
	}
	return true;
}

static void backup_path_of(char *buf, size_t n, size_t i)
{
	char dir[PATH_MAX];
	gs_wpath(dir, sizeof(dir), BACKUP_DIR);
	snprintf(buf, n, "%s/%zu", dir, i);
}

static void drop_backups(size_t count)
{
	char path[PATH_MAX + 32];
	for (size_t i = 0; i < count; i++) {
		backup_path_of(path, sizeof(path), i);
		unlink(path);
	}
	if (gs_wpath(path, sizeof(path), BACKUP_DIR) == 0)
		rmdir(path);
}

gs_status gs_initramfs_preflight(void)
{
	char tool[PATH_MAX], dir[PATH_MAX];
	if (!gs_initramfs_find(tool, sizeof(tool)))
		return GS_ERR_NO_INITRAMFS;
	size_t n = collect(before);
	unsigned long long total = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned long long size = (unsigned long long)before[i].size;
		total += size;
		/* Room for a new image next to the old one (mkinitcpio needs 1.25x). */
		gs_strlcpy(dir, before[i].path, sizeof(dir));
		*strrchr(dir, '/') = '\0';
		if (free_bytes(dir) < size + size / 2 + MARGIN) {
			fprintf(stderr, "gpushift: not enough free space next to %s\n", before[i].path);
			return GS_ERR_NO_SPACE;
		}
	}
	if (gs_wpath(dir, sizeof(dir), "/var/lib") == 0 && n && free_bytes(dir) < total + MARGIN) {
		fprintf(stderr, "gpushift: not enough free space in /var/lib for the image backups\n");
		return GS_ERR_NO_SPACE;
	}
	return GS_OK;
}

gs_status gs_regenerate_initramfs(void)
{
	char tool[PATH_MAX], lister[PATH_MAX], path[PATH_MAX + 32];
	const struct gs_initramfs_backend *b = gs_initramfs_find(tool, sizeof(tool));
	if (!b)
		return GS_ERR_NO_INITRAMFS;
	gs_status st = gs_initramfs_preflight();
	if (st != GS_OK)
		return st;

	size_t n = collect(before);
	if (gs_wpath(path, sizeof(path), BACKUP_DIR) < 0 || (mkdir(path, 0700) < 0 && errno != EEXIST))
		return GS_ERR_IO;
	for (size_t i = 0; i < n; i++) {
		backup_path_of(path, sizeof(path), i);
		if (copy_file(before[i].path, path, 0600) < 0) {
			fprintf(stderr, "gpushift: could not back up %s\n", before[i].path);
			drop_backups(n);
			return GS_ERR_IO;
		}
	}

	fprintf(stderr, "gpushift: regenerating the initramfs with %s...\n", b->name);
	bool ok = b->run(b, tool) == 0;
	if (!ok)
		fprintf(stderr, "gpushift: %s failed\n", b->name);

	/* Every image that existed must still be there, and every image must be valid. */
	const char *lst = gs_initramfs_lister(b, lister, sizeof(lister)) == 0 ? lister : NULL;
	size_t m = collect(after);
	for (size_t i = 0; ok && i < n; i++) {
		bool found = false;
		for (size_t j = 0; j < m && !found; j++)
			found = strcmp(before[i].path, after[j].path) == 0;
		if (!found)
			fprintf(stderr, "gpushift: %s disappeared\n", before[i].path);
		ok = found;
	}
	for (size_t j = 0; ok && j < m; j++)
		ok = image_ok(after[j].path, lst, b->lister_arg);
	if (ok) {
		drop_backups(n);
		return GS_OK;
	}

	gs_op_log("initramfs regeneration failed; restoring the previous images");
	for (size_t j = 0; j < m; j++) {
		bool old = false;
		for (size_t i = 0; i < n && !old; i++)
			old = strcmp(before[i].path, after[j].path) == 0;
		if (!old)
			unlink(after[j].path);
	}
	bool restored = true;
	for (size_t i = 0; i < n; i++) {
		backup_path_of(path, sizeof(path), i);
		if (restore_image(path, &before[i]) < 0) {
			fprintf(stderr, "gpushift: could not restore %s; its backup is %s\n",
				before[i].path, path);
			restored = false;
		}
	}
	if (restored)
		drop_backups(n);
	return GS_ERR_INITRAMFS_FAILED;
}
