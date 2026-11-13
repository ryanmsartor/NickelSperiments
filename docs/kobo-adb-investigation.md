# Kobo USB ADB investigation

## Goal and status

Build an installable Kobo package that starts an ADB daemon when a USB host is
connected and stops it on disconnect, with minimal idle overhead and no
polling. The initial preference was to preserve USB storage alongside ADB.
Mutually exclusive ADB and storage modes may be simpler, but have not been
validated.

**No package or repository implementation has been created.** Hardware testing
was stopped at the user's request after the reader rebooted unexpectedly
shortly after an ADB-only gadget was bound. The cause is unknown. Do not
install an automatic rule or repeat gadget binding without discussing the risk
with the user.

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
No automatic rules or persistent package were installed.

## Next steps

- First investigate the unexpected reboot with explicit user approval and a
  way to capture live kernel logs across the failure. Confirm the cable and
  host connection independently. Avoid unattended gadget binding until the
  failure is understood.
- Consider mutually exclusive modes: unbind ADB before Nickel exports storage,
  and only restart ADB after storage is safely detached and remounted. Define
  how the user selects modes and how boot, suspend, rapid reconnects, and
  failed handoffs are handled.
- Verify actual host add/remove events and udev 142's handling of spawned
  processes on this device. A rule can avoid a polling loop, but event delivery,
  daemon lifecycle, and startup while already connected must be tested.
- Choose and build a compatible daemon reproducibly; verify its license,
  root-access authentication, host key provisioning, and behavior on the
  reader. Do not assume a mutable nightly binary or the appearance of
  FunctionFS endpoints is sufficient.
- Test repeated connection, disconnection, reboot, mode switching, ADB
  authentication and transfers, and normal storage mounting and ejection.
  Treat support for other firmware versions as unverified until tested.

## References

- [KOReader Kobo USB event handling](https://github.com/koreader/koreader-base/blob/master/input/input-kobo.h)
- [KoboUSBMS storage setup](https://github.com/koreader/KoboUSBMS/blob/master/scripts/start-usbms.sh)
- [KoboUSBMS storage teardown](https://github.com/koreader/KoboUSBMS/blob/master/scripts/end-usbms.sh)
- [SDBD](https://github.com/openbfdev/sdbd)
