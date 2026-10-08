/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which modes a system supports, and which one is active or pending. */
#include "internal.h"

#include <string.h>

static const struct gs_gpu *only_gpu_of_kind(const gs_system *sys, gs_gpu_kind kind)
{
	const struct gs_gpu *found = NULL;
	for (size_t i = 0; i < sys->gpu_count; i++) {
		if (sys->gpus[i].kind != kind)
			continue;
		if (found)
			return NULL;
		found = &sys->gpus[i];
	}
	return found;
}

const struct gs_gpu *gs_igpu(const gs_system *sys)
{
	return sys->gpu_count == 2 ? only_gpu_of_kind(sys, GS_GPU_INTEGRATED) : NULL;
}

const struct gs_gpu *gs_dgpu(const gs_system *sys)
{
	return sys->gpu_count == 2 ? only_gpu_of_kind(sys, GS_GPU_DEDICATED) : NULL;
}

/* Hardware support only; switchability (laptop, conflicts) is checked separately. */
static bool mode_supported(const gs_system *sys, gs_mode mode)
{
	const struct gs_gpu *igpu = gs_igpu(sys), *dgpu = gs_dgpu(sys);
	if (!igpu || !dgpu)
		return false;
	/* The iGPU needs a driver and the panel, either directly or via the MUX. */
	bool igpu_panel = igpu->driver[0] && (igpu->internal_display || sys->mux);
	switch (mode) {
	case GS_MODE_INTEGRATED:
	case GS_MODE_HYBRID:
		return igpu_panel;
	case GS_MODE_DEDICATED:
		/* Routing the panel to a dGPU without a working driver means a black screen. */
		return sys->mux && !dgpu->removed && dgpu->driver[0];
	default:
		return false;
	}
}

gs_switch gs_switchability(const gs_system *sys)
{
	if (sys->gpu_count <= 1)
		return GS_SWITCH_SINGLE_GPU;
	if (!gs_sys_is_laptop(sys))
		return GS_SWITCH_DESKTOP;
	if (!mode_supported(sys, GS_MODE_HYBRID) && !mode_supported(sys, GS_MODE_DEDICATED))
		return GS_SWITCH_UNSUPPORTED;
	if (sys->conflict_count)
		return GS_SWITCH_CONFLICT;
	return GS_SWITCH_OK;
}

const char *gs_switch_message(gs_switch sw)
{
	switch (sw) {
	case GS_SWITCH_OK:
		return "GPU modes can be switched on this system.";
	case GS_SWITCH_SINGLE_GPU:
		return "This system has only one GPU, so there are no modes to switch.";
	case GS_SWITCH_DESKTOP:
		return "This is not a laptop. GPU modes are only managed on laptops; information only.";
	case GS_SWITCH_UNSUPPORTED:
		return "This GPU combination is not supported: GPUShift needs one integrated GPU "
		       "that can drive the internal display and one dedicated GPU.";
	case GS_SWITCH_CONFLICT:
		return "Another GPU switching tool is active. Remove or disable it before using GPUShift.";
	}
	return "";
}

size_t gs_list_modes(const gs_system *sys, gs_mode *modes)
{
	static const gs_mode all[GS_MODE_COUNT] = {
		GS_MODE_INTEGRATED, GS_MODE_HYBRID, GS_MODE_DEDICATED,
	};
	gs_switch sw = gs_switchability(sys);
	size_t n = 0;
	if (sw != GS_SWITCH_OK && sw != GS_SWITCH_CONFLICT)
		return 0;
	for (size_t i = 0; i < GS_MODE_COUNT; i++)
		if (mode_supported(sys, all[i]))
			modes[n++] = all[i];
	return n;
}

bool gs_mode_available(const gs_system *sys, gs_mode mode)
{
	gs_mode modes[GS_MODE_COUNT];
	size_t n = gs_list_modes(sys, modes);
	for (size_t i = 0; i < n; i++)
		if (modes[i] == mode)
			return true;
	return false;
}

gs_mode gs_current_mode(const gs_system *sys)
{
	if (sys->pending_to != GS_MODE_NONE && sys->pending_from != GS_MODE_NONE)
		return sys->pending_from;
	if (sys->has_state)
		return sys->state.mode;
	gs_switch sw = gs_switchability(sys);
	if (sw != GS_SWITCH_OK && sw != GS_SWITCH_CONFLICT)
		return GS_MODE_NONE;
	/* Nothing configured by GPUShift: the distribution default is hybrid. */
	return sys->mux && sys->mux->get(sys->mux) == GS_MODE_DEDICATED ? GS_MODE_DEDICATED
									  : GS_MODE_HYBRID;
}

bool gs_awaiting_confirmation(const gs_system *sys)
{
	/* The pending marker in /run disappears on reboot: only then can a change be judged. */
	return sys->has_state && sys->state.pending && sys->pending_to == GS_MODE_NONE;
}

int gs_unconfirmed_boots(const gs_system *sys)
{
	return gs_awaiting_confirmation(sys) ? sys->state.boot_attempts : 0;
}

const char *gs_recovery_text(void)
{
	return "If the screen stays black after rebooting:\n"
	       "  1. Wait and reboot: after 3 boots without confirmation the previous mode\n"
	       "     is restored automatically.\n"
	       "  2. Press Ctrl+Alt+F3, log in and run: sudo gpushift reset && sudo reboot\n"
	       "  3. In the boot menu press 'e', add gpushift.reset=1 to the 'linux' line\n"
	       "     and boot with Ctrl+X or F10.\n"
	       "Full guide: " GPUSHIFT_DOC_DIR "/RECOVERY.md\n";
}

gs_mode gs_pending_mode(const gs_system *sys)
{
	return sys->pending_to;
}

const char *gs_sys_mux_backend(const gs_system *sys)
{
	return sys->mux ? sys->mux->name : NULL;
}

static const char *const mode_names[] = {
	[GS_MODE_NONE] = "none",
	[GS_MODE_INTEGRATED] = "integrated",
	[GS_MODE_HYBRID] = "hybrid",
	[GS_MODE_DEDICATED] = "dedicated",
	[GS_MODE_DEFAULT] = "default",
};

const char *gs_mode_name(gs_mode mode)
{
	return (unsigned)mode <= GS_MODE_DEFAULT ? mode_names[mode] : "none";
}

bool gs_mode_from_name(const char *name, gs_mode *mode)
{
	for (unsigned i = GS_MODE_INTEGRATED; i <= GS_MODE_DEFAULT; i++) {
		if (strcmp(name, mode_names[i]) == 0) {
			*mode = (gs_mode)i;
			return true;
		}
	}
	return false;
}

const char *gs_strerror(gs_status status)
{
	switch (status) {
	case GS_OK: return "Success";
	case GS_ERR_GENERIC: return "Unexpected error";
	case GS_ERR_USAGE: return "Invalid arguments";
	case GS_ERR_SINGLE_GPU: return "Only one GPU is present; there are no modes to switch";
	case GS_ERR_NOT_SWITCHABLE: return "GPU switching is not supported on this system";
	case GS_ERR_MODE_UNAVAILABLE: return "The requested mode is not available on this system";
	case GS_ERR_CONFLICT: return "Another GPU switching tool is active";
	case GS_ERR_NO_INITRAMFS: return "No supported initramfs generator was found";
	case GS_ERR_IO: return "Could not write the system configuration";
	case GS_ERR_INITRAMFS_FAILED: return "Regenerating the initramfs failed; changes were rolled back";
	case GS_ERR_MUX: return "Could not change the firmware GPU MUX";
	case GS_ERR_PERMISSION: return "Administrator privileges are required";
	case GS_ERR_AUTH: return "Authentication was cancelled or denied";
	case GS_ERR_NOT_AWAITING: return "There is no unconfirmed mode change since the last reboot";
	}
	return "Unknown error";
}
