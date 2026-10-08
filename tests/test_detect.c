/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fixture.h"
#include "test.h"

static void test_single_intel(void)
{
	gs_system *sys = load_fixture("single-intel");
	CHECK(gs_gpu_count(sys) == 1);
	const gs_gpu *g = gs_gpu_at(sys, 0);
	CHECK_STR(gs_gpu_address(g), "0000:00:02.0");
	CHECK(gs_gpu_vendor_id(g) == 0x8086 && gs_gpu_device_id(g) == 0x9a49);
	CHECK(gs_gpu_get_kind(g) == GS_GPU_INTEGRATED);
	CHECK(gs_gpu_boot_vga(g));
	CHECK_STR(gs_gpu_driver(g), "i915");
	CHECK_STR(gs_gpu_driver_version(g), "6.12.48+deb13-amd64");
	CHECK_STR(gs_gpu_runtime_status(g), "active");
	CHECK_STR(gs_gpu_power_state(g), "D0");
	CHECK(gs_gpu_internal_display(g));
	CHECK(gs_gpu_vendor_name(g)[0] && gs_gpu_device_name(g)[0]);
	CHECK(gs_gpu_at(sys, 1) == NULL);

	CHECK_STR(gs_sys_distro(sys), "Debian GNU/Linux 13 (trixie)");
	CHECK_STR(gs_sys_kernel(sys), "6.12.48+deb13-amd64");
	CHECK(gs_sys_chassis_type(sys) == 10);
	CHECK_STR(gs_sys_chassis_name(sys), "Notebook");
	CHECK(gs_sys_is_laptop(sys));
	CHECK(gs_sys_secure_boot(sys) == GS_SECURE_BOOT_ENABLED);
	gs_system_free(sys);
}

static void test_single_amd(void)
{
	gs_system *sys = load_fixture("single-amd");
	CHECK(gs_gpu_count(sys) == 1);
	const gs_gpu *g = gs_gpu_at(sys, 0);
	CHECK(gs_gpu_get_kind(g) == GS_GPU_DEDICATED);
	CHECK_STR(gs_gpu_driver(g), "amdgpu");
	CHECK(gs_gpu_vram_bytes(g) == 8589934592ULL);
	CHECK(!gs_gpu_internal_display(g));
	CHECK(!gs_sys_is_laptop(sys));
	CHECK_STR(gs_sys_chassis_name(sys), "Desktop");
	CHECK(gs_sys_secure_boot(sys) == GS_SECURE_BOOT_LEGACY_BIOS);
	gs_system_free(sys);
}

static void test_intel_nvidia(void)
{
	gs_system *sys = load_fixture("intel-nvidia");
	CHECK(gs_gpu_count(sys) == 2); /* the HDMI audio function is not a GPU */
	const gs_gpu *igpu = gs_gpu_at(sys, 0), *dgpu = gs_gpu_at(sys, 1);
	CHECK(gs_gpu_get_kind(igpu) == GS_GPU_INTEGRATED);
	CHECK(gs_gpu_internal_display(igpu));
	CHECK_STR(gs_gpu_address(dgpu), "0000:01:00.0");
	CHECK(gs_gpu_get_kind(dgpu) == GS_GPU_DEDICATED);
	CHECK_STR(gs_gpu_driver(dgpu), "nvidia");
	CHECK_STR(gs_gpu_driver_version(dgpu), "580.82.09");
	CHECK_STR(gs_gpu_runtime_status(dgpu), "suspended");
	CHECK_STR(gs_gpu_power_state(dgpu), "D3cold");
	CHECK(gs_gpu_vram_bytes(dgpu) == 0); /* NVML is never used on fake trees */
	CHECK(!gs_gpu_internal_display(dgpu));
	CHECK(!gs_gpu_boot_vga(dgpu));
	gs_system_free(sys);
}

static void test_intel_nouveau(void)
{
	gs_system *sys = load_fixture("intel-nouveau");
	const gs_gpu *dgpu = gs_gpu_at(sys, 1);
	CHECK_STR(gs_gpu_driver(dgpu), "nouveau");
	CHECK_STR(gs_gpu_driver_version(dgpu), "6.16.10-arch1-1");
	/* The muxed eDP connector exists on the dGPU but is disconnected. */
	CHECK(!gs_gpu_internal_display(dgpu));
	CHECK(gs_gpu_internal_display(gs_gpu_at(sys, 0)));
	CHECK(gs_sys_secure_boot(sys) == GS_SECURE_BOOT_DISABLED);
	gs_system_free(sys);
}

static void test_amdapu_nvidia(void)
{
	gs_system *sys = load_fixture("amdapu-nvidia");
	CHECK(gs_gpu_count(sys) == 2);
	const gs_gpu *dgpu = gs_gpu_at(sys, 0), *apu = gs_gpu_at(sys, 1);
	CHECK_STR(gs_gpu_address(apu), "0000:05:00.0");
	CHECK(gs_gpu_get_kind(apu) == GS_GPU_INTEGRATED);
	CHECK(gs_gpu_get_kind(dgpu) == GS_GPU_DEDICATED);
	CHECK_STR(gs_gpu_driver(dgpu), "nvidia-open");
	CHECK_STR(gs_gpu_driver_version(dgpu), "580.82.09");
	gs_system_free(sys);
}

static void test_amdapu_amd(void)
{
	gs_system *sys = load_fixture("amdapu-amd");
	CHECK(gs_gpu_count(sys) == 2);
	const gs_gpu *dgpu = gs_gpu_at(sys, 0), *apu = gs_gpu_at(sys, 1);
	CHECK_STR(gs_gpu_address(apu), "0000:c4:00.0");
	CHECK(gs_gpu_get_kind(apu) == GS_GPU_INTEGRATED);
	CHECK(gs_gpu_get_kind(dgpu) == GS_GPU_DEDICATED);
	CHECK(gs_gpu_vram_bytes(dgpu) == 8589934592ULL);
	CHECK(gs_gpu_internal_display(apu) && !gs_gpu_internal_display(dgpu));
	gs_system_free(sys);
}

static void test_desktop(void)
{
	gs_system *sys = load_fixture("desktop-2gpu");
	CHECK(gs_gpu_count(sys) == 2);
	CHECK(!gs_sys_is_laptop(sys));
	CHECK(gs_gpu_boot_vga(gs_gpu_at(sys, 1)));
	gs_system_free(sys);
}

static void test_dgpu_without_driver(void)
{
	gs_system *sys = load_fixture("dgpu-nodriver");
	const gs_gpu *dgpu = gs_gpu_at(sys, 1);
	CHECK(gs_gpu_driver(dgpu) == NULL);
	CHECK(gs_gpu_driver_version(dgpu) == NULL);
	CHECK(gs_gpu_get_kind(dgpu) == GS_GPU_DEDICATED);
	gs_system_free(sys);
}

static void test_missing_root(void)
{
	gs_system *sys = load_fixture("does-not-exist");
	CHECK(gs_gpu_count(sys) == 0);
	CHECK_STR(gs_sys_distro(sys), "Unknown Linux");
	CHECK(!gs_sys_is_laptop(sys));
	gs_system_free(sys);
}

int main(void)
{
	test_single_intel();
	test_single_amd();
	test_intel_nvidia();
	test_intel_nouveau();
	test_amdapu_nvidia();
	test_amdapu_amd();
	test_desktop();
	test_dgpu_without_driver();
	test_missing_root();
	return TEST_RESULT();
}
