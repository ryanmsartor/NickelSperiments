# Kobo USB ADB investigation

## Goal and status

Build an installable Kobo package that starts an ADB daemon when a USB host is
connected and stops it on disconnect, with minimal idle overhead and no
polling. The initial preference was to preserve USB storage alongside ADB.
Mutually exclusive ADB and storage modes may be simpler, but have not been
validated.

**No package or automatic startup plumbing has been created.** A pinned EWDB
build recipe and successful manual ADB-only test now exist. An earlier SDBD
test was followed by an unexplained reboot, but the later EWDB test completed
without a reboot. Continue to treat gadget lifecycle changes cautiously.

## Device and observed firmware

- Investigation date: 2026-11-13.
- Kobo Clara BW, firmware 4.46.x, kernel `4.9.77`, `armv7l`.
  Support for earlier 4.x firmware is desirable but untested.
- `/sbin/udevd -d` is running; `udevadm --version` reports `142`.
- The stock udev rules invoke `/usr/local/Kobo/udev/plug` for `usb_host` and
  have rules for `usb_plug` and several USB controllers. KOReader also uses
  `usb_host` add/remove events for host connections. Event behavior on this
  particular firmware, including at boot or across suspend, remains unverified.
- `/sys/class/udc` contains `11211000.usb`.
- `/proc/config.gz` reports `CONFIG_USB_CONFIGFS=y`,
  `CONFIG_USB_CONFIGFS_MASS_STORAGE=y`,
  `CONFIG_USB_CONFIGFS_F_FS=y`, `CONFIG_USB_F_FS=y`, and
  `CONFIG_CONFIGFS_FS=y`. Configfs was mounted at `/sys/kernel/config`.
- Before the test, the existing gadget was `usb_gadget/g1`, unbound from its
  UDC, with `functions/mass_storage.0` and `configs/c.1`. The configuration
  already had a `mass_storage.0` symlink. `/mnt/onboard` was mounted locally.

## Nickel storage interaction

The device's `/usr/local/Kobo/udev/usb` uses `g1` on this MTK platform. On
storage enable it syncs and unmounts `/mnt/onboard`, configures the mass-storage
LUN, links the function, and binds `11211000.usb`. On storage disable it
unbinds the UDC, checks the filesystem, and remounts onboard storage.
`/usr/local/Kobo/udev/plug` forwards USB events to Nickel.

A separate ADB gadget and Nickel cannot both bind the same UDC. Starting a
daemon alone also does not create an ADB USB interface: FunctionFS and gadget
configuration are required. If ADB is active before the user chooses storage,
the handoff must release the UDC before Nickel uses it. Conversely, never
export onboard storage while it is mounted locally. The daemon, its
configuration, and authorized keys must remain accessible while
`/mnt/onboard` is unmounted.

When the user entered USB storage mode earlier in the investigation, the
Wi-Fi SSH connection was lost, preventing inspection of the live storage-mode
gadget. The exact reason for that SSH loss was not established.

For device access, the root account's home directory was `/`, not `/root`.
`ssh-copy-id` failed with `/bin/sh: ssh-rsa: not found`, and a noninteractive
SSH command hung after password authentication. An interactive `ssh -tt`
session worked. Temporary key-based access was set up in `/.ssh` and removed
after testing; future sessions will need to establish their own access.

## Tests performed

1. Examined the running udev rules, kernel configuration, UDC, gadget layout,
   and stock USB scripts. The configuration indicates that FunctionFS and
   mass storage are built in, but does not prove that they work together.
2. Evaluated the small ARMv7 static build of
   [SDBD](https://github.com/openbfdev/sdbd), an ADB-compatible daemon with
   public-key authentication. The downloaded nightly archive was
   `sdbd-linux-armv7-small.tar.gz`, SHA-256
   `30f1e8472653e9c91a876e6d2565aa97ddb314b4381a2bd87491a7ba36fdf407`.
   Nightly assets can change; this checksum identifies the tested download,
   not a reproducible build recipe or a trust assessment.
3. Copied the binary temporarily to `/tmp` on the reader. Its `--version`
   displayed `sdbd/v0.6`, and `--help` displayed its options; these invocations
   returned status 1. No daemon was installed permanently.
4. With the stock gadget unbound, attempted to create a separate configfs
   gadget. `mkdir` failed with `Cannot allocate memory` despite approximately
   129 MB free and 291 MB available. The reason is unknown; do not assume the
   kernel supports multiple independent gadgets.
5. In the existing unbound `g1`, created `functions/ffs.adb` and mounted
   FunctionFS at `/dev/usb-ffs/adb`. Started the temporary daemon with a
   temporary host public-key file, PID file, and log file. The FunctionFS
   `ep0`, `ep1`, and `ep2` entries appeared. This demonstrated daemon startup
   and descriptor setup, **not** a successful ADB connection or authentication.
6. To avoid exporting a locally mounted filesystem, temporarily unlinked
   `mass_storage.0` from `configs/c.1`, linked `ffs.adb` instead, and bound
   `g1` to `11211000.usb`. The UDC write succeeded, but its state read
   `not attached`. The host's `lsusb` and an isolated ADB server did not show
   the reader. No ADB shell, transfer, authentication, or disconnect test was
   completed.
7. SSH became unreachable, and the user subsequently reported that the Kobo
   rebooted on its own. The reboot happened after the binding test, but its
   precise timing and cause are not known. No `/proc/last_kmsg` or
   `/sys/fs/pstore` was available afterward; the new boot log did not establish
   the cause. The test did not capture live kernel logs.
8. Built EWDB from pinned source and tested it on the reader with a temporary
   ADB-only FunctionFS gadget. USB enumeration, root shell commands, push, pull,
   checksums, disconnect, and cleanup succeeded. See "Real-device result."

The test binary and ADB host key were used only for this investigation. The
test did not verify that the host cable, USB enumeration, or the daemon's
authentication were working.

## Cleanup and current state

The reboot cleared the temporary configfs gadget changes, FunctionFS mount,
daemon, and files under `/tmp`. After reboot, `/mnt/onboard` was mounted and
`/sys/kernel/config/usb_gadget` was empty; normal USB storage was **not**
retested. The temporary host ADB server and file-transfer server were stopped.
Temporary SSH and ADB keys and downloaded test files were removed from the
session, and the temporary SSH authorization was removed from the reader.
No automatic rules or persistent package were installed. A later EWDB test
also cleaned up its temporary gadget, FunctionFS mount, daemon, binary, and SSH
key. Its diagnostic logs remain on onboard storage.

## Next steps

- Test rapid reconnects, suspend/resume, reboot while connected, and failure
  paths. Confirm the storage handoff wrapper continues to preserve updated
  stock scripts across a firmware upgrade.
- Test repeated connection, disconnection, reboot, mode switching, ADB
  transfers, and normal storage mounting and ejection.
  Treat support for other firmware versions as unverified until tested.

## Daemon build validation

On 2026-11-13, SDBD was rejected as the reproducible-build candidate. Its
source at commit `90b543b5a777cf3897260ac1f39b4ac628ae2e02` requires `bfdev`
and `bfenv`; the public `bfenv` repository was unavailable, and its packaging
mirror did not provide a fetchable source revision. The published dependency
packages and daemon nightlies are not a complete source build recipe.

[EWDB](https://github.com/BrandonKowalski/ewdb) was built instead, pinned to
commit `3218c4eac664de5443e561f32ee9acf6e0ca0c17`. It is a pure-Go daemon
with vendored dependencies and explicit ARMv7 and FunctionFS support. The
build used the official Go 1.26.11 Linux ARM64 toolchain, whose archive SHA-256
was `6c2f29b458b5add0835f18b776f69c8881a6d88428f1484a6da55755e620757e`.

The resulting `build/ewdbd-linux-armv7` was a stripped, statically linked,
32-bit little-endian ARM EABI5 executable with SHA-256
`5dbca1067ed094422f85887fc0e41a254bb89f6aa06e02ac563c557fec4447e4`.
An immediate rebuild was byte-for-byte identical. It ran successfully through
the host's ARM32 compatibility support: `--version`, `help`, and
`getprop ro.product.cpu.abi` returned successfully, with the latter reporting
`armeabi-v7a`. The upstream unit tests passed; three Unix-socket tests initially
hit the workspace sandbox's `setsockopt` restriction and passed when rerun
outside that sandbox.

EWDB does not implement ADB RSA authentication at this revision. Its session
accepts the host's `CNXN` directly, and `AUTH` packets received outside a
session are ignored. Because the daemon runs as root, any USB host that can
connect receives root-level ADB access. For this experiment, the user accepted
physical USB access as the trust boundary. A future package must make this
automatic behavior explicit: connecting a USB host exposes root ADB unless and
until the user accepts Nickel's storage prompt.

The initial host-only work validated compilation and ordinary ARMv7 process
execution without changing Kobo state. The subsequent real-device test below
validated the FunctionFS and ADB transport. Authentication was intentionally
not tested because EWDB does not implement it and physical USB access was
accepted as the trust boundary.

Build the pinned binary with Go 1.26 or newer:

```sh
make ewdb-binary
```

### Real-device result

The binary was tested on the Kobo Clara BW on 2026-11-13. Its SHA-256 matched
the workstation artifact, `--version` ran successfully, and it reported the
`armeabi-v7a` ABI. A temporary ADB-only `g1` gadget used Google vendor/product
IDs `18d1:4ee7`; `/mnt/onboard` remained mounted locally and no mass-storage
function was linked.

EWDB opened the mounted FunctionFS endpoint and created `ep0`, `ep1`, and
`ep2`. After binding `g1` to `11211000.usb`, the UDC reached `configured`, the
host enumerated the reader at high speed with the expected vendor-specific ADB
interface and two bulk endpoints, and EWDB logged `BIND` and `ENABLE`.

The workstation's existing ADB server listed serial `KOBOADBTEST` as `device`.
The following operations succeeded:

- `adb shell id` returned `uid=0(root) gid=0(root) groups=0(root)`.
- `adb shell uname -a` reported the expected Kobo 4.9.77 ARMv7 kernel.
- A shell command returned the expected stdout.
- `adb push` and `adb pull` transferred a 3,810-byte test file. The original,
  device copy, and pulled copy had identical SHA-256
  `ee95b28a7ccc659641631a7a8ab57f5c25e2914fcbc5428302d79c70e3350dea`.

The gadget was then unbound. EWDB logged `DISABLE` and `UNBIND`; the UDC
returned to `not attached`, the temporary gadget and FunctionFS mount were
removed, and the host no longer listed the USB or ADB device. The temporary
binary, processes, transfer file, and SSH key were removed. The reader did not
reboot. Diagnostic logs were retained on the reader under
`/mnt/onboard/.kobo/ewdb-usb-test`.

### Mutually exclusive storage handoff

The first package prototype used a marker to select ADB instead of storage.
That design was replaced at the user's request: ADB now starts automatically
on every USB-host connection while Nickel still receives its ordinary storage
prompt. Ignoring or declining the prompt leaves ADB active.

If the prompt is accepted, `/usr/local/Kobo/udev/usb` is an EWDB wrapper that
stops the daemon, unbinds the UDC, unmounts FunctionFS, and removes `ffs.adb`
before executing the preserved stock storage script. The current firmware
script is backed up dynamically as `/usr/local/ewdb/kobo-usb-stock`; if a
firmware update replaces the wrapper, the next connection refreshes that
backup before reinstalling the handoff wrapper. The firmware's copy under
`/lib/udev/rules.d` is never modified.

A manual affirmative-handoff test succeeded. The host first saw ADB, then ADB
disconnected and the reader re-enumerated as stock `2237:4234` mass storage.
After the host safely unmounted the volume, the stock removal path checked the
filesystem and remounted `/mnt/onboard`; the UDC returned to `not attached` and
the reader did not reboot. Udev 142 also confirmed that the same-named rule in
`/etc/udev/rules.d` overrides the vendor rule and routes `usb_host` events to
the EWDB dispatcher.

The subsequent physical event test also passed. Reconnecting the cable started
ADB automatically, kept `/mnt/onboard` mounted locally, provided root shell and
mapped `getprop`, and transferred a test file with matching push/pull hashes.
Accepting Nickel's real prompt disconnected ADB and enumerated the stock
`2237:4237` mass-storage gadget. After a safe host unmount and physical cable
removal, `/mnt/onboard` remounted, EWDB stopped, the UDC was unbound, the
storage-active marker was absent, and the reader had not rebooted.

### ADB transfer-size failure

Follow-up testing on 2026-11-14 found that the earlier 3,810-byte transfer was
not representative of larger ADB SYNC transfers. With EWDB still pinned to
`3218c4eac664de5443e561f32ee9acf6e0ca0c17`, zero-filled `adb push` tests gave
the following results:

| Payload | Result |
| ---: | --- |
| 4 KiB | Passed; destination SHA-256 matched |
| 16 KiB | Passed; destination SHA-256 matched |
| 32 KiB | Passed; destination SHA-256 matched |
| 48 KiB | Passed; destination SHA-256 matched |
| 64 KiB | Failed with `failed to read copy response`; no destination file |
| 128 KiB | Failed during a ROM transfer; no destination file |
| 1.8 MiB | Failed during a benchmark-binary transfer; no destination file |

The 64 KiB test failed identically when the destination was moved from `/tmp`
to `/mnt/onboard/.kobo`, so this is not specific to the temporary filesystem.
The reader successfully created and hashed 1 MiB zero-filled files locally in
both locations. At the time of testing, `/tmp` also reported approximately
224 MiB free. These checks rule out destination capacity and ordinary file
writes as the limiting factor.

On every failed transfer, `/tmp/ewdbd.log` reported `session ended:
input/output error`, followed by EWDB deliberately reconnecting
`11211000.usb` and logging `DISABLE` and `ENABLE`. The EWDB process remained
running. Ordinary short `adb shell` commands worked, and shell command payloads
through 24 KiB were used successfully to stage the Gambatte benchmark in
chunks. A 90 KiB shell command payload also caused transport errors, while a
40.9 KiB payload was observed to arrive intact during exploratory testing.

This evidence places the defect in EWDB's ADB transport/framing path, or its
interaction with FunctionFS, rather than in the Kobo udev/storage-handoff
wrapper. The wrapper does not inspect ADB commands or SYNC traffic. A precise
upstream boundary still needs a protocol-level reproduction and EWDB debug
instrumentation before filing an issue.

Repeated forced gadget reconnects also coincided once with the workstation's
xHCI controller becoming unresponsive. The Linux host logged `xHCI host
controller not responding, assume dead`; the Kobo and a USB mouse disappeared
until the workstation was rebooted. Treat repeated failing-transfer tests as a
host-stability risk and keep future sweeps bounded.

## Future hardening plan

Every lifecycle test should preserve the same invariants:

- Exactly one mode owns the UDC: ADB, stock storage, or neither.
- `ffs.adb` and `mass_storage.0` are never linked into the active configuration
  together.
- `/mnt/onboard` is mounted locally in ADB mode and unmounted before its block
  device is assigned to mass storage.
- A failed ADB start falls back to Nickel's prompt without unmounting onboard
  storage.
- ADB is fully stopped and FunctionFS is unmounted before stock storage setup
  begins.
- After disconnect, `/mnt/onboard` is mounted, the UDC is unbound, no EWDB
  process remains, and `/run/ewdb/storage-active` is absent.

For each scenario, capture timestamps, `/tmp/ewdbd.log`, the relevant udev
environment, UDC state, configfs links, mount state, process state, host
`lsusb`, and `adb devices -l`. Preserve a synchronized `dmesg` snapshot on
onboard storage when testing a path that might reboot or drop networking.

### Rapid reconnects and event duplication

Test repeated plug/unplug cycles in ADB mode, repeated cycles after accepting
storage, and reconnects during each transition. Include unplugging while EWDB
is initializing, while an ADB transfer is active, immediately after accepting
storage, while the host is mounting/ejecting storage, and while Kobo's fsck is
running. Also inject duplicate and reordered `add`, `remove`, and `offline`
events into the dispatcher.

The scripts must remain idempotent: duplicate starts must not create a second
daemon or mount, duplicate stops must not enter the storage teardown path, and
an interrupted transition must converge to a safe unbound state. Add an
explicit transition/state model if event testing reveals ambiguity that the
current lock and runtime markers cannot resolve.

### Suspend and resume

Cover suspend while disconnected, while ADB is idle, during ADB traffic, while
Nickel's prompt is visible, and while stock storage is active. Test resume with
the cable still connected and with it removed during suspend.

Determine which udev or FunctionFS events the MTK controller emits on suspend
and resume. EWDB should either retain a usable session or close it cleanly and
be rediscovered by the host. It must not rebind over active mass storage, leave
a blocked FunctionFS endpoint, or prevent normal sleep. Measure idle wakeups
and suspend current to confirm that automatic ADB does not introduce polling or
material idle overhead.

### Reboot and startup ordering

Test reboot with USB disconnected, with ADB connected, with Nickel's prompt
visible, and with stock storage active after a safe host eject. Also test boot
with the cable already connected and boot after an intentionally interrupted
ADB or storage transition.

Verify when configfs, `/mnt/onboard`, udev, Nickel, and the UDC become ready.
The dispatcher should fail closed until onboard storage is mounted locally;
startup events must not be lost or start two owners. If udev does not emit a
usable event for an already-connected cable, add a one-shot boot reconciliation
step rather than a polling service. Reboot tests should also verify that `/run`
state is safely disposable and that no persistent marker is required for
recovery.

### Firmware upgrades and stock-script preservation

Test an upgrade that replaces `/usr/local/Kobo/udev/usb`, an upgrade that
replaces `/lib/udev/rules.d/kobo.rules`, and an upgrade that changes the stock
gadget layout or UDC name. On the next connection,
`install-storage-hook` should recognize a genuine new stock script, refresh
`/usr/local/ewdb/kobo-usb-stock`, and reinstall the wrapper atomically. It must
never back up the EWDB wrapper as though it were stock.

Before wrapping an updated script, consider recording and checking a hash plus
basic structural expectations, such as the MTK platform branch and safe
unmount-before-export ordering. Unknown UDCs or incompatible storage scripts
should disable ADB and leave the firmware's normal USB behavior intact. Repeat
the full ADB-to-storage handoff test after every supported firmware update.

### Failure injection and recovery

Exercise missing or non-executable EWDB binaries, malformed configuration,
FunctionFS mount failure, endpoint initialization timeout, occupied UDC,
configfs creation failure, daemon crash, failed unbind, and failed stock-script
backup or replacement. Separately test full `/run`, read-only root filesystem,
and low-memory conditions. Avoid injecting block-device or fsck failures on the
primary reader without a recoverable image and serial-console access.

Every pre-storage failure must keep `/mnt/onboard` mounted and still deliver
Nickel's prompt. A failed storage-hook installation must prevent automatic ADB
for that connection. Errors should be visible through syslog and a bounded
diagnostic log without creating a retry loop. Document a manual recovery
sequence that unbinds the UDC, stops EWDB, unmounts FunctionFS, restores the
stock USB script, reloads udev rules, and verifies onboard mount state.

### Compatibility, security, and packaging

Treat support as firmware- and platform-specific until the full lifecycle
matrix passes. Test other MTK Kobo models first, then evaluate older Freescale
and legacy gadget paths separately; do not assume their module-based storage
flow can use this configfs implementation.

Automatic ADB intentionally treats physical USB as the trust boundary and
grants unauthenticated root access. Keep that behavior prominent in package
documentation and release notes. Future options include an on-device disable
switch, a time-limited ADB window, host authentication, or a reduced-privilege
daemon, but none should weaken the safe storage handoff.

Make package builds reproducible in CI, publish the pinned EWDB revision and
artifact hashes, validate archive ownership/modes, and test installation plus
`/usr/local/ewdb/uninstall` on a clean firmware image. Uninstall testing must
cover ADB-active, disconnected, and refused-while-storage-active cases, and
must prove that the exact backed-up stock handler is restored before the udev
override is removed.

## References

- [KOReader Kobo USB event handling](https://github.com/koreader/koreader-base/blob/master/input/input-kobo.h)
- [KoboUSBMS storage setup](https://github.com/koreader/KoboUSBMS/blob/master/scripts/start-usbms.sh)
- [KoboUSBMS storage teardown](https://github.com/koreader/KoboUSBMS/blob/master/scripts/end-usbms.sh)
- [SDBD](https://github.com/openbfdev/sdbd)
