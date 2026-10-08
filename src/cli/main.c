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
	      "  modes         Show the current, pending and available GPU modes\n"
	      "\n"
	      "Options:\n"
	      "  --json        Machine-readable output (status, modes)\n"
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

static const char *mode_description(gs_mode mode)
{
	switch (mode) {
	case GS_MODE_INTEGRATED:
		return "dedicated GPU powered off and removed; lowest power use";
	case GS_MODE_HYBRID:
		return "integrated GPU drives the display, dedicated GPU on demand (PRIME offload)";
	case GS_MODE_DEDICATED:
		return "firmware MUX routes the display to the dedicated GPU";
	default:
		return "";
	}
}

static void modes_text(const gs_system *sys)
{
	gs_mode modes[GS_MODE_COUNT], cur = gs_current_mode(sys), pending = gs_pending_mode(sys);
	size_t n = gs_list_modes(sys, modes);
	gs_switch sw = gs_switchability(sys);

	if (sw != GS_SWITCH_OK)
		printf("%s\n", gs_switch_message(sw));
	for (size_t i = 0; i < gs_conflict_count(sys); i++)
		printf("Warning: conflicting tool active: %s\n", gs_conflict_name(sys, i));
	if (cur != GS_MODE_NONE)
		printf("Current mode:   %s\n", gs_mode_name(cur));
	if (pending != GS_MODE_NONE)
		printf("Pending mode:   %s (reboot required)\n", gs_mode_name(pending));
	if (gs_sys_mux_backend(sys))
		printf("Firmware MUX:   %s\n", gs_sys_mux_backend(sys));
	if (gs_sys_switcheroo(sys))
		printf("switcheroo-control is running (compatible)\n");
	if (!n)
		return;
	printf("Available modes:\n");
	for (size_t i = 0; i < n; i++)
		printf("  %c %-11s %s\n", modes[i] == cur ? '*' : ' ', gs_mode_name(modes[i]),
		       mode_description(modes[i]));
}

static const char *switch_name(gs_switch sw)
{
	static const char *const names[] = { "ok", "single-gpu", "desktop", "unsupported", "conflict" };
	return names[sw];
}

static void modes_json_fields(const gs_system *sys)
{
	gs_mode modes[GS_MODE_COUNT], cur = gs_current_mode(sys), pending = gs_pending_mode(sys);
	size_t n = gs_list_modes(sys, modes);
	gs_switch sw = gs_switchability(sys);

	printf("\"switchable\":\"%s\",\"message\":", switch_name(sw));
	json_str(gs_switch_message(sw));
	printf(",\"current\":");
	json_str(cur != GS_MODE_NONE ? gs_mode_name(cur) : NULL);
	printf(",\"pending\":");
	json_str(pending != GS_MODE_NONE ? gs_mode_name(pending) : NULL);
	printf(",\"reboot_pending\":%s,\"modes\":[", pending != GS_MODE_NONE ? "true" : "false");
	for (size_t i = 0; i < n; i++) {
		printf("%s", i ? "," : "");
		json_str(gs_mode_name(modes[i]));
	}
	printf("],\"mux\":");
	json_str(gs_sys_mux_backend(sys));
	printf(",\"conflicts\":[");
	for (size_t i = 0; i < gs_conflict_count(sys); i++) {
		printf("%s", i ? "," : "");
		json_str(gs_conflict_name(sys, i));
	}
	printf("],\"switcheroo_control\":%s", gs_sys_switcheroo(sys) ? "true" : "false");
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
	printf("\nMode\n");
	modes_text(sys);
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
	printf("],");
	modes_json_fields(sys);
	printf("}\n");
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
			return GS_ERR_USAGE;
		}
	}
	if (!cmd || (strcmp(cmd, "status") != 0 && strcmp(cmd, "modes") != 0)) {
		if (cmd)
			fprintf(stderr, "gpushift: unknown command '%s'\n", cmd);
		usage(stderr);
		return GS_ERR_USAGE;
	}

	gs_system *sys = gs_detect();
	if (!sys) {
		fprintf(stderr, "gpushift: out of memory\n");
		return EXIT_FAILURE;
	}
	if (strcmp(cmd, "modes") == 0 && json) {
		putchar('{');
		modes_json_fields(sys);
		printf("}\n");
	} else if (strcmp(cmd, "modes") == 0) {
		modes_text(sys);
	} else if (json) {
		status_json(sys);
	} else {
		status_text(sys);
	}
	gs_system_free(sys);
	return EXIT_SUCCESS;
}
