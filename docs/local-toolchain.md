# Local Kobo toolchain

Kobo C and C++ targets use [NickelTC](https://github.com/pgaskin/NickelTC), the same GCC 4.9.4 toolchain and Kobo sysroot used by the build workflows. Keeping the local compiler aligned with CI avoids accidentally linking against the workstation's newer ARM libraries.

## Workspace layout

The local installation is stored under `.toolchains/`, which is excluded from Git because it is host-specific and approximately 1.5 GB:

```text
.toolchains/
  bin/arm-nickel-linux-gnueabihf-g++
  nickeltc-root/                         extracted NickelTC 1.0 image
  qemu/usr/bin/qemu-x86_64              local ARM64 QEMU build
```

NickelTC 1.0 distributes x86-64 compiler programs. This workstation is ARM64, so the compiler cannot run directly. `scripts/nickeltc-exec` starts each x86-64 program with the workspace copy of QEMU and uses the extracted image as its x86-64 loader prefix.

GCC normally launches several child programs. The installed copies of `cc1`, `cc1plus`, and `as` contain local wrappers which route their original `.x86_64` programs through `nickeltc-exec`. Kernel `binfmt_misc` registration is therefore unnecessary.

The kernel on this workstation does not permit the PRoot-based alternative. For Ko Boy links, `scripts/nickeltc-link` invokes NickelTC's BFD linker directly with the same startup objects, sysroot, static C++ runtime, and Kobo dynamic loader selected by GCC. Ko Boy does not use LTO, so it does not need GCC's otherwise nested `collect2` process.

## Building

`mk/kobo-toolchain.mk` detects the workspace compiler automatically. `mk/armgba.mk` owns the emulator backend and its checks, while `mk/koboy.mk` owns the application and its diagnostics. Normal targets need no environment setup:

```sh
make koboy-display-test
make koboy-framebuffer-isolation
make koboy
make koboy-supervisor
make koboy-lifecycle
make armgba-adapter-check
make armgba-core-check
make armgba-libretro-check
make armgba-probe
```

The armGBA checks also use NickelTC's C compiler through `scripts/nickeltc-tool`. Its machine core is compiled explicitly for ARMv7-A, NEON, and the hard-float ABI used by the Kobo firmware.

All Ko Boy and armGBA C and C++ compilations generate `.d` files beside their object files. The top-level build includes these files automatically, so changing a header rebuilds every dependent object and prevents stale incremental links. The dependency files live under `build/obj/` and are removed by the existing clean targets.

To rebuild every armGBA and Ko Boy object after changing toolchains, use:

```sh
make armgba-clean koboy-clean
make koboy-display-test koboy-framebuffer-isolation koboy koboy-supervisor
make koboy-lifecycle
```

Confirm that an output is for the Kobo rather than the workstation:

```sh
file build/koboy
readelf -h build/koboy
readelf --version-info build/koboy
```

The expected result is a 32-bit ARM EABI5 hard-float executable using `/lib/ld-linux-armhf.so.3`. At the time of setup, all Ko Boy outputs required no libc symbol newer than `GLIBC_2.17`.

## CI and other hosts

The GitHub workflow runs inside `ghcr.io/pgaskin/nickeltc:1.0` and places the native x86-64 compiler on `PATH`. When `.toolchains/bin/arm-nickel-linux-gnueabihf-g++` is absent, the Makefile retains that normal behavior and uses `arm-nickel-linux-gnueabihf-g++` from `PATH` for both compilation and linking.

The direct linker helper is intentionally limited to this repository's non-LTO Ko Boy targets and accepts only object files, archives, an output path, and the two configured static-runtime flags. It is not a general replacement for the full NickelTC driver or the Qt build used by Ko Boy's Nickel lifecycle plugin.

## Reinstalling

The installed toolchain came from the content-addressed layers of `ghcr.io/pgaskin/nickeltc:1.0`; the layer checksums were validated before extraction. QEMU came from Debian's ARM64 `qemu-user` package. Download archives were removed after validation and can be fetched again if the local installation is damaged.

When reinstalling, preserve the directory names above because the Makefile and helper scripts resolve them relative to the repository root. Recreate the compiler-stage wrappers or use system-wide x86-64 `binfmt_misc` support before removing the local wrappers.
