/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fixture.h"
#include "test.h"

#include <sys/stat.h>
#include <unistd.h>

static size_t modes_of(const char *fixture, gs_mode *modes, gs_switch *sw)
{
	gs_system *sys = load_fixture(fixture);
	size_t n = gs_list_modes(sys, modes);
	*sw = gs_switchability(sys);
	gs_system_free(sys);
	return n;
}

static void test_not_switchable(void)
{
	gs_mode m[GS_MODE_COUNT];
	gs_switch sw;
	CHECK(modes_of("single-intel", m, &sw) == 0 && sw == GS_SWITCH_SINGLE_GPU);
	CHECK(modes_of("single-amd", m, &sw) == 0 && sw == GS_SWITCH_SINGLE_GPU);
	CHECK(modes_of("desktop-2gpu", m, &sw) == 0 && sw == GS_SWITCH_DESKTOP);

	gs_system *sys = load_fixture("single-intel");
	CHECK(gs_current_mode(sys) == GS_MODE_NONE);
	CHECK(gs_pending_mode(sys) == GS_MODE_NONE);
	CHECK(!gs_mode_available(sys, GS_MODE_HYBRID));
	gs_system_free(sys);
}

static void test_hybrid_laptops(void)
{
	static const char *const fixtures[] = {
		"intel-nvidia", "intel-nouveau", "amdapu-nvidia", "amdapu-amd", "dgpu-nodriver",
	};
	for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
		gs_mode m[GS_MODE_COUNT];
		gs_switch sw;
		size_t n = modes_of(fixtures[i], m, &sw);
		if (n != 2 || sw != GS_SWITCH_OK)
			fprintf(stderr, "fixture %s\n", fixtures[i]);
		CHECK(sw == GS_SWITCH_OK);
		CHECK(n == 2 && m[0] == GS_MODE_INTEGRATED && m[1] == GS_MODE_HYBRID);
	}
	gs_system *sys = load_fixture("intel-nvidia");
	CHECK(gs_current_mode(sys) == GS_MODE_HYBRID);
	CHECK(gs_sys_mux_backend(sys) == NULL);
	CHECK(!gs_mode_available(sys, GS_MODE_DEDICATED));
	gs_system_free(sys);
}

static void test_asus_mux(void)
{
	gs_system *sys = load_fixture("asus-mux");
	gs_mode m[GS_MODE_COUNT];
	CHECK_STR(gs_sys_mux_backend(sys), "asus-wmi");
	CHECK(gs_list_modes(sys, m) == 3);
	CHECK(gs_mode_available(sys, GS_MODE_DEDICATED));
	CHECK(gs_current_mode(sys) == GS_MODE_HYBRID); /* gpu_mux_mode=1 */
	gs_system_free(sys);
}

static void test_conflict(void)
{
	gs_system *sys = load_fixture("conflict");
	CHECK(gs_switchability(sys) == GS_SWITCH_CONFLICT);
	CHECK(gs_conflict_count(sys) == 1);
	CHECK_STR(gs_conflict_name(sys, 0), "supergfxctl");
	CHECK(gs_conflict_name(sys, 1) == NULL);
	CHECK(gs_sys_switcheroo(sys));
	/* Modes are still listed so the user sees what would be possible. */
	CHECK(gs_mode_available(sys, GS_MODE_HYBRID));
	gs_system_free(sys);

	sys = load_fixture("intel-nvidia");
	CHECK(gs_conflict_count(sys) == 0 && !gs_sys_switcheroo(sys));
	gs_system_free(sys);
}

static void write_file(const char *dir, const char *rel, const char *content)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s%s", dir, rel);
	FILE *f = fopen(path, "w");
	CHECK(f != NULL);
	if (f) {
		fputs(content, f);
		fclose(f);
	}
}

/* An Integrated-mode system where the dGPU is gone from the bus, with a reboot pending. */
static void test_state_and_pending(void)
{
	char root[] = "/tmp/gpushift-test-XXXXXX", path[1024];
	CHECK(mkdtemp(root) != NULL);
	const char *dirs[] = { "/var", "/var/lib", "/var/lib/gpushift", "/run", "/run/gpushift" };
	for (size_t i = 0; i < 5; i++) {
		snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
		CHECK(mkdir(path, 0755) == 0);
	}
	setenv("GPUSHIFT_ETC_ROOT", root, 1);

	write_file(root, "/var/lib/gpushift/state",
		   "version=1\nmode=integrated\ndgpu=0000:01:00.0\ndgpu_vendor=10de\n"
		   "dgpu_device=25a2\nmux_backend=\nmux_orig=\n");
	gs_system *sys = load_fixture("single-intel");
	CHECK(gs_gpu_count(sys) == 2);
	const gs_gpu *ghost = gs_gpu_at(sys, 1);
	CHECK(gs_gpu_removed(ghost));
	CHECK(gs_gpu_vendor_id(ghost) == 0x10de && gs_gpu_device_id(ghost) == 0x25a2);
	CHECK(gs_gpu_get_kind(ghost) == GS_GPU_DEDICATED);
	CHECK(gs_switchability(sys) == GS_SWITCH_OK);
	CHECK(gs_current_mode(sys) == GS_MODE_INTEGRATED);
	CHECK(gs_pending_mode(sys) == GS_MODE_NONE);
	CHECK(gs_mode_available(sys, GS_MODE_HYBRID));
	gs_system_free(sys);

	write_file(root, "/run/gpushift/pending", "from=hybrid\nto=integrated\n");
	sys = load_fixture("single-intel");
	CHECK(gs_current_mode(sys) == GS_MODE_HYBRID);
	CHECK(gs_pending_mode(sys) == GS_MODE_INTEGRATED);
	gs_system_free(sys);

	/* A dGPU that is still present is not duplicated. */
	sys = load_fixture("intel-nvidia");
	CHECK(gs_gpu_count(sys) == 2 && !gs_gpu_removed(gs_gpu_at(sys, 1)));
	gs_system_free(sys);

	write_file(root, "/var/lib/gpushift/state", "mode=bogus\n");
	sys = load_fixture("intel-nvidia");
	CHECK(gs_current_mode(sys) == GS_MODE_HYBRID); /* from pending.from */
	gs_system_free(sys);

	const char *files[] = { "/var/lib/gpushift/state", "/run/gpushift/pending" };
	for (size_t i = 0; i < 2; i++) {
		snprintf(path, sizeof(path), "%s%s", root, files[i]);
		unlink(path);
	}
	for (size_t i = 5; i-- > 0;) {
		snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
		rmdir(path);
	}
	rmdir(root);
	unsetenv("GPUSHIFT_ETC_ROOT");
}

static void test_names(void)
{
	gs_mode m;
	CHECK(gs_mode_from_name("dedicated", &m) && m == GS_MODE_DEDICATED);
	CHECK(!gs_mode_from_name("none", &m) && !gs_mode_from_name("Hybrid", &m));
	CHECK_STR(gs_mode_name(GS_MODE_INTEGRATED), "integrated");
	CHECK_STR(gs_strerror(GS_ERR_SINGLE_GPU), "Only one GPU is present; there are no modes to switch");
	CHECK(gs_switch_message(GS_SWITCH_DESKTOP)[0] != '\0');
}

int main(void)
{
	test_not_switchable();
	test_hybrid_laptops();
	test_asus_mux();
	test_conflict();
	test_state_and_pending();
	test_names();
	return TEST_RESULT();
}
