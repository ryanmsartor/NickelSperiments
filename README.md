# NickelSperiments

NickelSperiments is a collection of independent experiments for exploring and
extending Kobo e-readers. Projects share a repository and build infrastructure,
but each Speriment has its own purpose, maturity, and deliverables.

## Speriments

### EWDB for Kobo

The EWDB package provides root ADB access over USB on supported Kobo devices.
ADB is available automatically when a USB host is connected and yields to
Kobo's normal USB-storage mode when the user accepts Nickel's storage prompt.
Physical USB access is the trust boundary; connected hosts are not
authenticated.

This Speriment currently targets the tested MediaTek-based Kobo Clara BW.

- [Kobo USB ADB investigation and technical details](docs/kobo-adb-investigation.md)

### Ko Boy

Ko Boy is a Game Boy, Game Boy Color, Game Boy Advance, and Mega Duck player
for Kobo e-readers, built around armGBA. It combines full-speed emulation with
an e-ink-specific display scheduler, touchscreen controls, an on-device game
browser, persistent saves and states, suspend handling, and supervised return
to Nickel after normal or abnormal termination.

Ko Boy has been developed and tested primarily on the MediaTek-based Kobo
Clara BW. It is not yet distributed as an end-user installation package. No
ROMs are included.

- [Ko Boy usage and launcher setup](docs/koboy.md)
- [Ko Boy port plan and technical details](docs/koboy-port-plan.md)

## Building

The top-level Makefile exposes separate targets for each Speriment. Run
`make help` for the current build and packaging commands. GitHub Actions also
builds each package through its own project-specific workflow.

Release packages use project-specific filenames so their contents are clear.
Kobo installation archives must be renamed to `KoboRoot.tgz` before being
placed in the device's `.kobo` directory.
