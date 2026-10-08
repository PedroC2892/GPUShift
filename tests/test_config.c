/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/lib/internal.h"
#include "fixture.h"
#include "test.h"

static gs_system *sys;
static char modprobe[4096], udev[4096];

static int build(const char *fixture, gs_mode mode)
{
	if (sys)
		gs_system_free(sys);
	sys = load_fixture(fixture);
	return gs_config_build(sys, mode, modprobe, sizeof(modprobe), udev, sizeof(udev));
}

int main(void)
{
	CHECK(build("intel-nvidia", GS_MODE_INTEGRATED) == 0);
	CHECK(strstr(modprobe, "blacklist nvidia\nalias nvidia off\n") != NULL);
	CHECK(strstr(modprobe, "blacklist nouveau\n") != NULL);
	CHECK(strstr(modprobe, "i915") == NULL);
	CHECK(strstr(udev, "KERNEL==\"0000:01:00.*\", ATTR{vendor}==\"0x10de\"") != NULL);
	CHECK(strstr(udev, "ATTR{power/control}=\"auto\", ATTR{remove}=\"1\"") != NULL);

	CHECK(build("intel-nvidia", GS_MODE_HYBRID) == 0);
	CHECK(strstr(modprobe, "options nvidia-drm modeset=1\n") != NULL);
	CHECK(strstr(modprobe, "options nvidia NVreg_DynamicPowerManagement=0x02\n") != NULL);
	CHECK(strstr(udev, "ACTION==\"bind\", SUBSYSTEM==\"pci\", KERNEL==\"0000:01:00.0\"") != NULL);

	CHECK(build("asus-mux", GS_MODE_DEDICATED) == 0);
	CHECK(strstr(modprobe, "options nvidia-drm modeset=1\n") != NULL);
	CHECK(strstr(modprobe, "NVreg_DynamicPowerManagement") == NULL);
	CHECK(udev[0] == '\0');

	/* AMD APU + AMD dGPU: amdgpu drives the panel and must never be blacklisted. */
	CHECK(build("amdapu-amd", GS_MODE_INTEGRATED) == 0);
	CHECK(strstr(modprobe, "amdgpu") == NULL);
	CHECK(strstr(modprobe, "blacklist radeon\n") != NULL);
	CHECK(strstr(udev, "KERNEL==\"0000:03:00.*\", ATTR{vendor}==\"0x1002\"") != NULL);
	CHECK(build("amdapu-amd", GS_MODE_HYBRID) == 0);
	CHECK(modprobe[0] == '\0' && udev[0] == '\0');

	CHECK(build("single-intel", GS_MODE_HYBRID) < 0);
	CHECK(build("intel-nvidia", GS_MODE_DEFAULT) < 0);

	CHECK(gs_valid_pci_address("0000:01:00.0"));
	CHECK(gs_valid_pci_address("0000:c4:00.1"));
	CHECK(!gs_valid_pci_address("0000:01:00.0\""));
	CHECK(!gs_valid_pci_address("0000:01:00"));
	CHECK(!gs_valid_pci_address("0000:01:0g.0"));

	gs_system_free(sys);
	return TEST_RESULT();
}
