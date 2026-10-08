# GPUShift

GPUShift is a GPU mode manager for Linux. It detects the GPUs in the system,
shows detailed information about them and lets you switch between the GPU
modes the hardware actually supports (Integrated, Hybrid and, on laptops with
a firmware MUX, Dedicated).

It only relies on kernel interfaces (sysfs, procfs, modprobe.d, udev) and
polkit, so it works on any modern distribution and with any desktop
environment, on both Wayland and X11.

The project is under active development. See the sections below as they are
filled in.

## Building

```sh
cmake -B build -DGPUSHIFT_BUILD_GUI=OFF
cmake --build build
ctest --test-dir build
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
