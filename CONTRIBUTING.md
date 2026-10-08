# Contributing to GPUShift

## Workflow

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # ASan + UBSan
cmake --build build
ctest --test-dir build --output-on-failure
```

Build with both GCC and Clang (`CC=clang CXX=clang++`) before sending a
change; CI does the same on Debian, Ubuntu, Fedora and Arch.

- C17 for the library, CLI and helper (POSIX libc and libpci only), C++17 and
  Qt6 Widgets for the GUI. The GUI only presents what the library decides.
- Tabs for indentation, English for code, comments and messages.
- Commits are small, build on their own and pass the tests. Messages follow
  Conventional Commits (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`,
  `build:`, `ci:`, `chore:`).
- Never test against the real system: use fixtures and temporary prefixes.

## Layout

| Path | What |
|------|------|
| `include/gpushift/gpushift.h` | Public C API |
| `src/lib/` | libgpushift: detection, modes, MUX, conflicts, state, initramfs |
| `src/helper/helper.c` | `gpushift-helper`, the only program that writes |
| `src/cli/main.c` | `gpushift` |
| `src/gui/` | `gpushift-gui` |
| `tests/fixtures/` | Fake `/sys`, `/proc` and `/etc` trees, one per scenario |
| `tests/*.c`, `tests/*.sh` | Unit tests and black-box CLI and helper tests |

## Adding a firmware MUX backend

MUX backends are rows in the `backends` table of `src/lib/mux.c`:

```c
{
	.name = "vendor-wmi",
	.mux_path = "/sys/devices/platform/vendor-wmi/gpu_mux",
	.dgpu_value = "1",       /* value that routes the panel to the dGPU */
	.hybrid_value = "0",     /* value that routes the panel to the iGPU */
	.disable_path = NULL,    /* optional attribute that powers the dGPU off */
	SYSFS_BACKEND,
},
```

1. Add the row. `SYSFS_BACKEND` gives the generic probe/get/set functions for
   a plain sysfs attribute; an interface that is not one attribute can set
   its own `probe`, `get` and `set` functions instead. The first backend whose
   probe succeeds is used, so put more specific interfaces first.
2. Add a fixture in `tests/fixtures/` with the attribute and its current value
   (copy `asus-mux` as a starting point).
3. Extend `tests/test_modes.c` (backend detected, Dedicated listed) and
   `tests/helper_test.sh` (apply `dedicated` writes the right value, `reset`
   restores the original one).
4. Mention the backend in the README hardware list.

## Adding an initramfs generator

Generators are rows in the `backends` table of `src/lib/initramfs.c`:

```c
{ "mytool", "mytool", NULL, { "--regenerate", "--all-kernels" }, exec_generator },
```

The fields are the display name, the binary searched in `/usr/sbin`,
`/usr/bin`, `/sbin` and `/bin`, an optional absolute program to run instead
of the binary (as `booster` does), up to four arguments and the run function.
Rows are tried in order, so place it where it will not shadow the default
generator of another distribution. Then add a case to the generator loop in
`tests/helper_test.sh`, which installs a fake binary in the temporary prefix
and checks the arguments it received, and list it in the README.

## Adding a conflicting tool

Add a row to `tools` in `src/lib/conflicts.c` with the daemon name (matched
against `argv[0]` or `argv[1]` of running processes) and/or the files that
only exist while the tool is in use. Add a fixture or extend `conflict`.

## Fixtures

A fixture is a directory that mirrors the paths GPUShift reads, for example
`sys/bus/pci/devices/0000:01:00.0/{vendor,device,class,boot_vga}`, a
`driver` symlink whose last component is the driver name,
`drm/card0/card0-eDP-1/status`, `sys/class/dmi/id/chassis_type`,
`proc/sys/kernel/osrelease` and `etc/os-release`. Point the CLI at one to try
it:

```sh
GPUSHIFT_SYSFS_ROOT=tests/fixtures/asus-mux build/src/gpushift status
```

## Translations

The GUI uses Qt Linguist. After changing strings:

```sh
lupdate -no-obsolete -locations none src/gui -ts translations/gpushift_pt_PT.ts
linguist translations/gpushift_pt_PT.ts
```

To add a language, create `translations/gpushift_<lang>.ts` with `lupdate`
and add it to `TS_FILES` in `src/gui/CMakeLists.txt`; also translate the
desktop entry and the polkit messages in `data/`.
