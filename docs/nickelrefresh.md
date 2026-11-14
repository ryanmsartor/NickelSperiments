# NickelRefresh

NickelRefresh uses NickelHook to load into Nickel and FBInk to draw a small
test patch through Kobo's framebuffer update API. NickelTC builds the plugin
for Kobo.

## Test behavior

The test sweeps `A2`, `DU`, and `GC16` waveform modes at target request rates
of 1, 2, 4, 8, 12, 16, and 20 updates per second. Each step lasts three
seconds. It logs actual request timestamps and call durations to
`/mnt/onboard/.kobo/NickelRefresh.csv`; a high-speed camera is needed to
measure when the panel visibly changes.

The sweep is deliberately limited to standard waveform modes. It does not
change global display settings or tune undocumented panel timings.

## Build

Clone the repository with its submodules and build with the NickelTC Docker
image:

```sh
git clone https://github.com/ryanmsartor/NickelRefresh.git NickelSperiments
cd NickelSperiments
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 \
  bash -lc 'git submodule update --init; export PATH="/tc/arm-nickel-linux-gnueabihf/bin:$PATH"; make nickelrefresh-package'
```

The package target produces `KoboRoot.tgz`. It builds FBInk as a small static
library with framebuffer drawing and region-dump support, then links it into
the NickelHook plugin. FBInk's i2c-tools dependency is pinned to a GitHub
mirror of the same upstream repository, at FBInk's pinned commit, because
git.kernel.org is not reachable in all build environments.

## Install and run

1. Copy `KoboRoot.tgz` to `.kobo/KoboRoot.tgz` on the Kobo's USB storage and
   safely eject it.
2. To opt in, create an empty `.kobo/NickelRefresh.start` file, then restart
   Nickel or reboot.
3. The plugin removes the start file, runs the approximately three-minute
   sweep, restores the framebuffer patch, and appends measurements to
   `.kobo/NickelRefresh.csv`.
4. Film the patch at 120 or 240 frames per second under steady lighting and
   compare visible transitions with the CSV request timestamps.

Without the start file, the plugin does nothing. The test changes only a
160-by-160-pixel patch and attempts to restore its original framebuffer
contents afterward. Keep the device on a stable screen during the test because
Nickel may redraw that area. A normal full-screen refresh should clear the
patch if restoration fails.

## Interpreting results

The CSV records the mode, target rate, elapsed time, sequence number, request
timestamp, call duration, and FBInk result for each update. A 20 Hz target is
only a request cadence. Determine the highest visible, sustained rate by
checking that the video shows a distinct transition for each request at a
stable interval without accumulating delay or skipped updates. Fast modes
trade image quality for speed, and repeated partial updates can ghost.

## Dependencies and licensing

- [NickelTC](https://github.com/pgaskin/NickelTC) supplies the cross-toolchain.
- [NickelHook](https://github.com/pgaskin/NickelHook) loads the opt-in plugin.
- [FBInk](https://github.com/NiLuJe/FBInk) handles framebuffer drawing,
  waveform selection, and restoration.

FBInk is GPL-3.0-or-later; review its license before redistributing builds.
