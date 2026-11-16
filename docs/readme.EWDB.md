# eWaste Debug Bridge Daemon (EWDB) for Kobo

## Scope and security model

EWDB for Kobo packages [EWDB](https://github.com/BrandonKowalski/ewdb) as an event-driven, root ADB service for Kobo e-readers. On the tested Kobo Clara BW, ADB starts automatically when a USB host is connected. Nickel still shows its normal storage prompt; accepting that prompt stops ADB before Kobo's stock mass-storage path takes ownership of USB. Ignoring or declining the prompt leaves ADB active.

EWDB does not implement ADB host authentication at the pinned revision. Any connected USB host receives a root shell, so physical USB access is the trust boundary. This package should not be installed where that policy is unacceptable.

The package has been tested on a MediaTek-based Kobo Clara BW running firmware 4.46.x and Linux 4.9.77. Support for other models, controller families, and firmware revisions is not established.

## Building and installing

The build script pins EWDB commit `9ae15fa774ca584dafd6bdb57a2dec906c157442`, builds from its vendored dependencies, and requires Go 1.26 or newer:

```sh
make ewdb-binary
make ewdb-package
```

`make ewdb-package` creates `build/EWDB-KoboRoot.tgz`. Rename it to `KoboRoot.tgz`, copy it into the Kobo's `.kobo` directory, and safely eject the reader. The archive installs the daemon, configuration, event scripts, udev override, uninstaller, and the license texts for EWDB and its bundled dependencies under `/usr/local/ewdb`.

To uninstall while USB storage is inactive and `/mnt/onboard` is mounted locally, run:

```sh
/usr/local/ewdb/uninstall
```

The uninstaller stops ADB, restores the preserved stock USB handler, removes the udev override and EWDB files, and reloads the udev rules. It deliberately refuses to run while storage mode is active or if the stock handler cannot be restored safely.

## Runtime design

The package overrides the firmware's `usb_host` udev rule with `/etc/udev/rules.d/kobo.rules`. `usb-event` serializes host add and remove events, refreshes the stock storage hook, and starts or stops the ADB gadget. The scripts reuse the firmware's existing `g1` configfs gadget, mount FunctionFS at `/dev/usb-ffs/adb`, link `ffs.adb`, and bind the MediaTek UDC at `11211000.usb`. They do not export `/mnt/onboard` or link the mass-storage function while ADB owns the controller.

Interactive `adb shell` requires Unix98 pseudoterminals. Some Kobo firmware exposes `/dev/ptmx` without mounting devpts, which makes opening the visible device return `ENOENT`. `ewdb-usb start` therefore mounts `/dev/pts` idempotently before starting EWDB, using `gid=5,mode=620,ptmxmode=666` with a fallback for kernels that reject `ptmxmode`.

The device configuration keeps EWDB's ADB `maxdata` at 32 KiB. The tested MediaTek `mtu3` controller rejects a 64 KiB ADB payload after FunctionFS framing expands the request to 66,048 bytes, beyond the controller's 65,532-byte request ceiling. `maxdata` limits individual transport messages, not total transfer size, so multi-megabyte pushes and pulls still work.

## Storage handoff

`install-storage-hook` preserves the current `/usr/local/Kobo/udev/usb` as `/usr/local/ewdb/kobo-usb-stock` and installs a small wrapper in its place. If a firmware update replaces the wrapper, the next host connection preserves the new stock script before reinstalling the hook. The firmware copy under `/lib/udev/rules.d` is not modified.

When the user accepts Nickel's storage prompt, the wrapper stops EWDB, unbinds the UDC, removes `ffs.adb`, and unmounts FunctionFS before invoking the preserved stock script. The stock script then unmounts `/mnt/onboard` and exports its block device. This ordering keeps ADB and mass storage mutually exclusive and never exports a filesystem which remains mounted locally.

## Validation completed

Real-device testing has covered automatic enumeration, root command and interactive shells, repeated disconnects, the Nickel storage prompt, safe storage ejection, and restoration of `/mnt/onboard`. Checksum-verified `adb push` and `adb pull` transfers passed through multi-megabyte sizes after restoring the 32 KiB transport limit. Installation and removal have also been exercised on the Clara BW.

During the original investigation, a separate SDBD experiment and an attempt to create a second configfs gadget proved unreliable. The shipped design uses EWDB and the firmware's existing unbound `g1` gadget instead. Those discarded prototypes are not part of the package.

## Operational invariants

- Exactly one mode owns the UDC: ADB, stock storage, or neither.
- `ffs.adb` and `mass_storage.0` are never linked into the active configuration together.
- `/mnt/onboard` stays mounted locally in ADB mode and is unmounted before its block device is assigned to mass storage.
- A failed ADB start leaves onboard storage mounted and preserves Nickel's opportunity to enter storage mode.
- ADB is stopped and FunctionFS is unmounted before stock storage setup begins.
- After disconnect, `/mnt/onboard` is mounted, the UDC is unbound, EWDB is stopped, and `/run/ewdb/storage-active` is absent.

## Future hardening

Further qualification should cover rapid and reordered USB events, suspend and resume during transfers or storage handoff, boot with USB already connected, interrupted transitions, low-memory and read-only-root failures, firmware upgrades which change Kobo's USB scripts, and additional Kobo hardware. Unknown UDCs or incompatible stock scripts should eventually fail closed to the firmware's normal USB behavior.

Security hardening avenues include host authentication, an on-device disable control, a time-limited ADB window, or a reduced-privilege daemon. Until one is implemented, releases must state prominently that connecting a USB host grants unauthenticated root access.
