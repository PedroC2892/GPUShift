/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *env_root(const char *name)
{
	const char *v = getenv(name);
	return v ? v : "";
}

const char *gs_read_root(void)
{
	return env_root("GPUSHIFT_SYSFS_ROOT");
}

const char *gs_write_root(void)
{
	return env_root("GPUSHIFT_ETC_ROOT");
}

static int vpath(char *buf, size_t n, const char *root, const char *fmt, va_list ap)
{
	size_t len = strlen(root);
	if (len >= n)
		return -1;
	memcpy(buf, root, len);
	int r = vsnprintf(buf + len, n - len, fmt, ap);
	return (r < 0 || (size_t)r >= n - len) ? -1 : 0;
}

int gs_rpath(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vpath(buf, n, gs_read_root(), fmt, ap);
	va_end(ap);
	return r;
}

int gs_wpath(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vpath(buf, n, gs_write_root(), fmt, ap);
	va_end(ap);
	return r;
}

int gs_read_file(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	size_t total = 0;
	while (total + 1 < n) {
		ssize_t r = read(fd, buf + total, n - 1 - total);
		if (r < 0 && errno == EINTR)
			continue;
		if (r < 0) {
			close(fd);
			return -1;
		}
		if (r == 0)
			break;
		total += (size_t)r;
	}
	close(fd);
	buf[total] = '\0';
	return 0;
}

int gs_read_line(const char *path, char *buf, size_t n)
{
	if (gs_read_file(path, buf, n) < 0)
		return -1;
	buf[strcspn(buf, "\n")] = '\0';
	size_t len = strlen(buf);
	while (len > 0 && isspace((unsigned char)buf[len - 1]))
		buf[--len] = '\0';
	return 0;
}

int gs_read_ulong(const char *path, unsigned long *out)
{
	char buf[64], *end;
	if (gs_read_line(path, buf, sizeof(buf)) < 0 || !buf[0])
		return -1;
	errno = 0;
	unsigned long v = strtoul(buf, &end, 0);
	if (errno || *end)
		return -1;
	*out = v;
	return 0;
}

bool gs_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

void gs_strlcpy(char *dst, const char *src, size_t n)
{
	if (!n)
		return;
	size_t len = strnlen(src, n - 1);
	memcpy(dst, src, len);
	dst[len] = '\0';
}
