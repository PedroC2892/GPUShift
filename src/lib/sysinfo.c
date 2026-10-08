/* SPDX-License-Identifier: GPL-3.0-or-later */
/* General system information: distribution, kernel, chassis, session, Secure Boot. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#define EFI_GLOBAL_GUID "8be4df61-93ca-11d2-aa0d-e98c0e3fa5a8"

static void read_distro(struct gs_system *sys)
{
	static const char *const files[] = { "/etc/os-release", "/usr/lib/os-release" };
	char path[PATH_MAX], buf[4096];
	for (size_t i = 0; i < 2; i++) {
		if (gs_rpath(path, sizeof(path), "%s", files[i]) < 0 ||
		    gs_read_file(path, buf, sizeof(buf)) < 0)
			continue;
		for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
			if (strncmp(line, "PRETTY_NAME=", 12) != 0)
				continue;
			char *v = line + 12;
			size_t len = strlen(v);
			if (len >= 2 && (v[0] == '"' || v[0] == '\'') && v[len - 1] == v[0]) {
				v[len - 1] = '\0';
				v++;
			}
			gs_strlcpy(sys->distro, v, sizeof(sys->distro));
			return;
		}
	}
	gs_strlcpy(sys->distro, "Unknown Linux", sizeof(sys->distro));
}

static void read_kernel(struct gs_system *sys)
{
	char path[PATH_MAX];
	struct utsname u;
	if (gs_rpath(path, sizeof(path), "/proc/sys/kernel/osrelease") == 0 &&
	    gs_read_line(path, sys->kernel, sizeof(sys->kernel)) == 0 && sys->kernel[0])
		return;
	if (!gs_read_root()[0] && uname(&u) == 0)
		gs_strlcpy(sys->kernel, u.release, sizeof(sys->kernel));
}

static void read_secure_boot(struct gs_system *sys)
{
	char path[PATH_MAX];
	unsigned char data[8];
	if (gs_rpath(path, sizeof(path), "/sys/firmware/efi") < 0 || !gs_exists(path)) {
		sys->secure_boot = GS_SECURE_BOOT_LEGACY_BIOS;
		return;
	}
	/* efivarfs: 4 bytes of attributes followed by the 1-byte value. */
	if (gs_rpath(path, sizeof(path), "/sys/firmware/efi/efivars/SecureBoot-" EFI_GLOBAL_GUID) < 0)
		return;
	FILE *f = fopen(path, "rb");
	if (!f) {
		/* Firmware without the variable has no Secure Boot support. */
		sys->secure_boot = GS_SECURE_BOOT_DISABLED;
		return;
	}
	size_t n = fread(data, 1, sizeof(data), f);
	fclose(f);
	if (n == 5)
		sys->secure_boot = data[4] == 1 ? GS_SECURE_BOOT_ENABLED : GS_SECURE_BOOT_DISABLED;
}

void gs_detect_sysinfo(struct gs_system *sys)
{
	char path[PATH_MAX];
	unsigned long v;
	read_distro(sys);
	read_kernel(sys);
	if (gs_rpath(path, sizeof(path), "/sys/class/dmi/id/chassis_type") == 0 &&
	    gs_read_ulong(path, &v) == 0)
		sys->chassis_type = (int)v;
	const char *s = getenv("XDG_SESSION_TYPE");
	gs_strlcpy(sys->session_type, s ? s : "", sizeof(sys->session_type));
	s = getenv("XDG_CURRENT_DESKTOP");
	gs_strlcpy(sys->desktop, s ? s : "", sizeof(sys->desktop));
	read_secure_boot(sys);
}

const char *gs_sys_distro(const gs_system *sys) { return sys->distro; }
const char *gs_sys_kernel(const gs_system *sys) { return sys->kernel; }
int gs_sys_chassis_type(const gs_system *sys) { return sys->chassis_type; }
const char *gs_sys_session_type(const gs_system *sys) { return sys->session_type[0] ? sys->session_type : NULL; }
const char *gs_sys_desktop(const gs_system *sys) { return sys->desktop[0] ? sys->desktop : NULL; }
gs_secure_boot gs_sys_secure_boot(const gs_system *sys) { return sys->secure_boot; }

/* SMBIOS 3.x chassis types (DSP0134, 7.4.1). */
const char *gs_sys_chassis_name(const gs_system *sys)
{
	static const char *const names[] = {
		[1] = "Other", [2] = "Unknown", [3] = "Desktop", [4] = "Low Profile Desktop",
		[5] = "Pizza Box", [6] = "Mini Tower", [7] = "Tower", [8] = "Portable",
		[9] = "Laptop", [10] = "Notebook", [11] = "Hand Held", [12] = "Docking Station",
		[13] = "All in One", [14] = "Sub Notebook", [15] = "Space-saving", [16] = "Lunch Box",
		[17] = "Main Server Chassis", [23] = "Rack Mount Chassis", [24] = "Sealed-case PC",
		[30] = "Tablet", [31] = "Convertible", [32] = "Detachable", [33] = "IoT Gateway",
		[34] = "Embedded PC", [35] = "Mini PC", [36] = "Stick PC",
	};
	int t = sys->chassis_type;
	if (t > 0 && t < (int)(sizeof(names) / sizeof(names[0])) && names[t])
		return names[t];
	return "Unknown";
}

bool gs_sys_is_laptop(const gs_system *sys)
{
	switch (sys->chassis_type) {
	case 8: case 9: case 10: case 11: case 14: case 30: case 31: case 32:
		return true;
	case 0: case 1: case 2:
		/* Unknown chassis: an internal panel is the best evidence of a laptop. */
		for (size_t i = 0; i < sys->gpu_count; i++)
			if (sys->gpus[i].internal_display)
				return true;
		return false;
	default:
		return false;
	}
}

const char *gs_secure_boot_name(gs_secure_boot sb)
{
	switch (sb) {
	case GS_SECURE_BOOT_ENABLED: return "enabled";
	case GS_SECURE_BOOT_DISABLED: return "disabled";
	case GS_SECURE_BOOT_LEGACY_BIOS: return "unsupported (legacy BIOS)";
	default: return "unknown";
	}
}
