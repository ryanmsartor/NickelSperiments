# NickelSperiments

NickelSperiments is a collection of tools and experiments for Kobo e-readers.
It is a home for independent projects that explore, test, and extend the same
hardware rather than a single application. The first included experiment is
NickelRefresh, an opt-in Kobo Nickel mod for measuring sustained e-paper
refresh requests.

For the separate USB ADB investigation, see
[Kobo USB ADB investigation](docs/kobo-adb-investigation.md).

## EWDB: USB debugging experiment

The repository can build a pinned ARMv7 EWDB daemon and an experimental Kobo
package:

```sh
make ewdb-package
```

This requires Go 1.26 or newer and produces
`build/EWDB-KoboRoot.tgz`. On the tested Kobo Clara BW firmware, installing the
package makes ADB available whenever a USB host is connected while preserving
Nickel's normal USB-storage prompt. If the user accepts that prompt, the
handoff wrapper fully releases ADB before the stock firmware unmounts and
exports onboard storage. ADB and storage are never linked into the gadget at
the same time.

EWDB does not authenticate the host. While ADB is active, physical USB access
provides a root shell. This package is experimental and currently targets the
tested MTK UDC `11211000.usb`; unsupported devices fail closed and retain
Kobo's stock prompt path.

To remove the experiment and restore Kobo's backed-up stock USB handler:

```sh
/usr/local/ewdb/uninstall
```

## Project builds

The top-level makefile dispatches to project-specific definitions under `mk/`.
Run `make help` to list the public targets.

- `make nickelrefresh` builds the NickelRefresh plugin.
- `make nickelrefresh-package` builds its `KoboRoot.tgz` with NickelTC.
- `make ewdb-binary` builds the pinned static ARMv7 daemon.
- `make ewdb-package` builds `build/EWDB-KoboRoot.tgz`.

NickelRefresh and EWDB have separate GitHub Actions workflows, toolchains, and
named artifacts. The older `ewdbd` and `ewdb-koboroot` targets remain as
compatibility aliases.

## NickelRefresh: refresh-rate experiment

NickelRefresh uses NickelHook to load into Nickel and FBInk to draw a small
test patch using Kobo's framebuffer update API. NickelTC builds the mod for
Kobo.

The test sweeps `A2`, `DU`, and `GC16` waveform modes at target request rates of
1, 2, 4, 8, 12, 16, and 20 updates per second. Each step lasts three seconds.
It logs the actual request timestamps and call durations to
`/mnt/onboard/.kobo/NickelRefresh.csv`; a high-speed camera is still needed to
measure when the panel visibly changes.

## Build

Clone with its submodules and build with the NickelTC Docker image:

```sh
git clone https://github.com/ryanmsartor/NickelRefresh.git NickelSperiments
cd NickelSperiments
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 \
  bash -lc 'git submodule update --init; export PATH="/tc/arm-nickel-linux-gnueabihf/bin:$PATH"; make nickelrefresh-package'
```

The build produces `KoboRoot.tgz`. It builds FBInk as a small static library
with framebuffer drawing and region-dump support, then links it into the
NickelHook plugin. FBInk's i2c-tools dependency is pinned to a GitHub mirror
of the same upstream repository, at FBInk's pinned commit, because
git.kernel.org is not reachable in all build environments.

## Install and run

1. Copy `KoboRoot.tgz` to the root of the Clara BW's USB storage and safely
   eject it. NickelHook installs the plugin under `/usr/local/Kobo`.
2. To opt in, create an empty file named
   `.kobo/NickelRefresh.start` on the device's USB storage, then safely eject
   and restart Nickel (or reboot).
3. The plugin removes the start file, runs the approximately three-minute
   sweep, restores the framebuffer patch, and appends measurements to
   `.kobo/NickelRefresh.csv`.
4. Film the patch at 120/240 fps under steady lighting to measure visible
   transitions. Compare the video with the CSV's request timestamps; the
   driver may queue or merge requests, so request rate is not panel frame rate.

Without the start file the plugin does nothing. The test only changes a
160-by-160-pixel patch and attempts to restore its original framebuffer
contents afterward. Keep the device on a stable screen during the test; Nickel
may redraw that area while the test is running. If the framebuffer restore
fails, a normal full-screen refresh should clear the patch.

## Interpreting results

The CSV records the mode, target rate, elapsed time, sequence number, request
timestamp, call duration, and FBInk result for each update. A 20 Hz target is
only a request cadence. Determine the highest *visible, sustained* rate by
checking that the video shows a distinct transition for each request at a
stable interval, without accumulating delay or skipped updates. Fast modes
trade image quality for speed; repeated partial updates can ghost.

The sweep is deliberately limited to standard `A2`, `DU`, and `GC16` modes. It
does not change global display settings or attempt to tune undocumented panel
timings.

## Dependencies

- [NickelTC](https://github.com/pgaskin/NickelTC) supplies the cross-toolchain.
- [NickelHook](https://github.com/pgaskin/NickelHook) loads the opt-in mod.
- [FBInk](https://github.com/NiLuJe/FBInk) handles Kobo framebuffer drawing,
  waveform selection, and restoration.

FBInk is GPL-3.0-or-later; review its license before redistributing builds.
