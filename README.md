# NickelRefresh

NickelRefresh is an opt-in Kobo Nickel mod for measuring sustained e-paper
refresh requests. It uses NickelHook to load into Nickel and FBInk to draw a
small test patch using Kobo's framebuffer update API. NickelTC builds the mod
for Kobo.

The test sweeps `A2`, `DU`, and `GC16` waveform modes at target request rates of
1, 2, 4, 8, 12, 16, and 20 updates per second. Each step lasts three seconds.
It logs the actual request timestamps and call durations to
`/mnt/onboard/.kobo/NickelRefresh.csv`; a high-speed camera is still needed to
measure when the panel visibly changes.

## Build

Clone with its submodules and build with the NickelTC Docker image:

```sh
git clone https://github.com/ryanmsartor/NickelRefresh.git
cd NickelRefresh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 \
  bash -lc 'git submodule update --init; export PATH="/tc/arm-nickel-linux-gnueabihf/bin:$PATH"; make clean all koboroot'
```

The build produces `KoboRoot.tgz`. It builds FBInk as a small static library
with framebuffer drawing and region-dump support, then links it into the
NickelHook plugin. FBInk's i2c-tools dependency is pinned to a GitHub mirror
revision because the upstream git.kernel.org host is not reachable in all
build environments.

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
