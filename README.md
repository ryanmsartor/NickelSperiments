# NickelSperiments

NickelSperiments is a collection of independent experiments for exploring and extending Kobo e-readers. Projects share a repository and build infrastructure, but each Speriment has its own purpose, maturity, and deliverables.

## Speriments

### EWDB for Kobo

The EWDB package provides root ADB access over USB on supported Kobo devices. ADB is available automatically when a USB host is connected and yields to Kobo's normal USB-storage mode when the user accepts Nickel's storage prompt. Physical USB access is the trust boundary; connected hosts are not authenticated.

This Speriment currently targets the tested MediaTek-based Kobo Clara BW.

- [Kobo USB ADB investigation and technical details](docs/kobo-adb-investigation.md)

### Ko Boy

Ko Boy is a Game Boy, Game Boy Color, Game Boy Advance, Mega Duck, NES, Famicom Disk System, and Gamate player for Kobo e-readers, built around [armGBA](https://github.com/MagnominusGit/armGBA), [armNES](https://github.com/MagnominusGit/armNES), and [superwhizkid](https://github.com/ryanmsartor/superwhizkid). It combines full-speed emulation with an e-ink-specific display scheduler, touchscreen and (WIP) Bluetooth controller input, optional Bluetooth audio, an on-device game browser, persistent saves and states, suspend handling, and supervised return to Nickel after normal or abnormal termination.

Ko Boy has been developed and tested primarily on the MediaTek-based Kobo Clara BW. The build produces a user-installable KoboRoot archive; support outside the tested device remains unverified. No ROMs or BIOS files are included.

- [Ko Boy guide, technical details, and release status](docs/readme.KOBOY.md)

## Building

The top-level Makefile exposes separate targets for each Speriment. Run `make help` for the current build and packaging commands. GitHub Actions also builds each package through its own project-specific workflow.

Release packages use project-specific filenames so their contents are clear. Kobo installation archives must be renamed to `KoboRoot.tgz` before being placed in the device's `.kobo` directory.


## Support the author

The NickelSperiments project is, and will remain indefinitely, free and open source. 

That said, if you like my work and would like to support me, donations are welcome. 

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/H2H7BO8ZE)
