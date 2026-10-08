/* SPDX-License-Identifier: GPL-3.0-or-later */
/* gpushift: command line interface to libgpushift. */
#include "gpushift/gpushift.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out)
{
	fputs("Usage: gpushift <command> [--json]\n"
	      "\n"
	      "Commands:\n"
	      "  status        Show system and GPU information\n"
	      "\n"
	      "Options:\n"
	      "  --json        Machine-readable output (status)\n"
	      "  -h, --help    Show this help\n"
	      "  --version     Show the version\n", out);
}

static void json_str(const char *s)
{
	if (!s) {
		fputs("null", stdout);
		return;
	}
	putchar('"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\')
			printf("\\%c", c);
		else if (c < 0x20)
			printf("\\u%04x", c);
		else
			putchar(c);
	}
	putchar('"');
}

static const char *or_dash(const char *s) { return s ? s : "-"; }

static void print_vram(uint64_t bytes)
{
	if (bytes >= (1ULL << 30))
		printf("%.1f GiB", (double)bytes / (1ULL << 30));
	else
		printf("%" PRIu64 " MiB", bytes >> 20);
}

static void status_text(const gs_system *sys)
{
	const char *session = gs_sys_session_type(sys), *desktop = gs_sys_desktop(sys);
	printf("System\n");
	printf("  Distribution:   %s\n", gs_sys_distro(sys));
	printf("  Kernel:         %s\n", gs_sys_kernel(sys));
	printf("  Chassis:        %s%s\n", gs_sys_chassis_name(sys),
	       gs_sys_is_laptop(sys) ? " (laptop)" : "");
	printf("  Session:        %s%s%s%s\n", or_dash(session), desktop ? " (" : "",
	       desktop ? desktop : "", desktop ? ")" : "");
	printf("  Secure Boot:    %s\n", gs_secure_boot_name(gs_sys_secure_boot(sys)));

	for (size_t i = 0; i < gs_gpu_count(sys); i++) {
		const gs_gpu *g = gs_gpu_at(sys, i);
		printf("\nGPU %zu: %s %s\n", i, gs_gpu_vendor_name(g), gs_gpu_device_name(g));
		printf("  Address:        %s [%04x:%04x]\n", gs_gpu_address(g),
		       gs_gpu_vendor_id(g), gs_gpu_device_id(g));
		printf("  Type:           %s%s\n", gs_gpu_kind_name(gs_gpu_get_kind(g)),
		       gs_gpu_boot_vga(g) ? " (boot VGA)" : "");
		if (gs_gpu_removed(g)) {
			printf("  State:          removed from the PCI bus by GPUShift\n");
			continue;
		}
		if (gs_gpu_driver(g))
			printf("  Driver:         %s (version %s)\n", gs_gpu_driver(g),
			       or_dash(gs_gpu_driver_version(g)));
		else
			printf("  Driver:         none\n");
		printf("  Power:          %s, %s\n", or_dash(gs_gpu_runtime_status(g)),
		       or_dash(gs_gpu_power_state(g)));
		printf("  VRAM:           ");
		if (gs_gpu_vram_bytes(g))
			print_vram(gs_gpu_vram_bytes(g));
		else
			printf("unknown");
		printf("\n  Internal panel: %s\n", gs_gpu_internal_display(g) ? "yes" : "no");
	}
}

static void status_json(const gs_system *sys)
{
	printf("{\"system\":{\"distro\":");
	json_str(gs_sys_distro(sys));
	printf(",\"kernel\":");
	json_str(gs_sys_kernel(sys));
	printf(",\"chassis_type\":%d,\"chassis\":", gs_sys_chassis_type(sys));
	json_str(gs_sys_chassis_name(sys));
	printf(",\"laptop\":%s,\"session_type\":", gs_sys_is_laptop(sys) ? "true" : "false");
	json_str(gs_sys_session_type(sys));
	printf(",\"desktop\":");
	json_str(gs_sys_desktop(sys));
	printf(",\"secure_boot\":");
	json_str(gs_secure_boot_name(gs_sys_secure_boot(sys)));
	printf("},\"gpus\":[");
	for (size_t i = 0; i < gs_gpu_count(sys); i++) {
		const gs_gpu *g = gs_gpu_at(sys, i);
		printf("%s{\"address\":", i ? "," : "");
		json_str(gs_gpu_address(g));
		printf(",\"vendor_id\":\"%04x\",\"device_id\":\"%04x\",\"vendor\":",
		       gs_gpu_vendor_id(g), gs_gpu_device_id(g));
		json_str(gs_gpu_vendor_name(g));
		printf(",\"model\":");
		json_str(gs_gpu_device_name(g));
		printf(",\"type\":");
		json_str(gs_gpu_kind_name(gs_gpu_get_kind(g)));
		printf(",\"boot_vga\":%s,\"removed\":%s,\"driver\":",
		       gs_gpu_boot_vga(g) ? "true" : "false", gs_gpu_removed(g) ? "true" : "false");
		json_str(gs_gpu_driver(g));
		printf(",\"driver_version\":");
		json_str(gs_gpu_driver_version(g));
		printf(",\"runtime_status\":");
		json_str(gs_gpu_runtime_status(g));
		printf(",\"power_state\":");
		json_str(gs_gpu_power_state(g));
		printf(",\"vram_bytes\":%" PRIu64 ",\"internal_display\":%s}", gs_gpu_vram_bytes(g),
		       gs_gpu_internal_display(g) ? "true" : "false");
	}
	printf("]}\n");
}

int main(int argc, char **argv)
{
	const char *cmd = NULL;
	bool json = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			json = true;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return EXIT_SUCCESS;
		} else if (strcmp(argv[i], "--version") == 0) {
			printf("gpushift %s\n", gs_version());
			return EXIT_SUCCESS;
		} else if (!cmd && argv[i][0] != '-') {
			cmd = argv[i];
		} else {
			fprintf(stderr, "gpushift: unexpected argument '%s'\n", argv[i]);
			usage(stderr);
			return 2;
		}
	}
	if (!cmd || strcmp(cmd, "status") != 0) {
		if (cmd)
			fprintf(stderr, "gpushift: unknown command '%s'\n", cmd);
		usage(stderr);
		return 2;
	}

	gs_system *sys = gs_detect();
	if (!sys) {
		fprintf(stderr, "gpushift: out of memory\n");
		return EXIT_FAILURE;
	}
	if (json)
		status_json(sys);
	else
		status_text(sys);
	gs_system_free(sys);
	return EXIT_SUCCESS;
}
