/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Initramfs generators. The first one found is used, in table order. Only
 * fixed absolute directories are searched, never $PATH. To support another
 * generator, add a row.
 */
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const search_dirs[] = { "/usr/sbin", "/usr/bin", "/sbin", "/bin" };

int gs_spawn(const char *path, char *const argv[], char *const envp[], bool quiet)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		int null = quiet ? open("/dev/null", O_WRONLY) : -1;
		if (null >= 0)
			dup2(null, STDOUT_FILENO);
		execve(path, argv, envp);
		_exit(127);
	}
	int status;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool executable(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static int exec_generator(const struct gs_initramfs_backend *b, const char *path)
{
	char runner[PATH_MAX];
	if (b->runner) {
		if (gs_wpath(runner, sizeof(runner), "%s", b->runner) < 0 || !executable(runner))
			return -1;
		path = runner;
	}
	char *argv[6] = { (char *)path };
	for (size_t i = 0; i < 4 && b->args[i]; i++)
		argv[i + 1] = (char *)b->args[i];
	static char *const envp[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL=C", NULL };
	return gs_spawn(path, argv, envp, false) == 0 ? 0 : -1;
}

static const struct gs_initramfs_backend backends[] = {
	/* Debian, Ubuntu and derivatives (initramfs-tools). */
	{ "update-initramfs", "update-initramfs", NULL, { "-u", "-k", "all" }, "lsinitramfs", NULL,
	  exec_generator },
	/* Arch Linux default. */
	{ "mkinitcpio", "mkinitcpio", NULL, { "-P" }, "lsinitcpio", NULL, exec_generator },
	/* Fedora, RHEL, openSUSE. */
	{ "dracut", "dracut", NULL, { "--force", "--regenerate-all" }, "lsinitrd", NULL, exec_generator },
	/* booster ships a script that rebuilds the image of every installed kernel. */
	{ "booster", "booster", "/usr/lib/booster/regenerate_images", { NULL }, "booster", "ls",
	  exec_generator },
};

int gs_initramfs_lister(const struct gs_initramfs_backend *b, char *path, size_t n)
{
	for (size_t d = 0; d < sizeof(search_dirs) / sizeof(search_dirs[0]); d++)
		if (gs_wpath(path, n, "%s/%s", search_dirs[d], b->lister) == 0 && executable(path))
			return 0;
	return -1;
}

const struct gs_initramfs_backend *gs_initramfs_find(char *path, size_t n)
{
	for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
		for (size_t d = 0; d < sizeof(search_dirs) / sizeof(search_dirs[0]); d++)
			if (gs_wpath(path, n, "%s/%s", search_dirs[d], backends[i].binary) == 0 &&
			    executable(path))
				return &backends[i];
	return NULL;
}
