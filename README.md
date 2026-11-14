# NickelSperiments

NickelSperiments is a collection of independent experiments for exploring and
extending Kobo e-readers. Projects share a repository and build infrastructure,
but each Speriment has its own purpose, maturity, and deliverables.

## Speriments

### NickelRefresh

NickelRefresh is an opt-in Nickel plugin for measuring sustained e-paper
refresh requests. It exercises standard Kobo waveform modes at several target
rates, records request timing, and displays a small test patch suitable for
high-speed-camera analysis.

NickelRefresh is intended as a measurement tool, not as a permanent display
configuration or general refresh-rate modification.

[NickelRefresh usage and technical details](docs/nickelrefresh.md)

### EWDB for Kobo

The EWDB package provides root ADB access over USB on supported Kobo devices.
ADB is available automatically when a USB host is connected and yields to
Kobo's normal USB-storage mode when the user accepts Nickel's storage prompt.
Physical USB access is the trust boundary; connected hosts are not
authenticated.

This Speriment currently targets the tested MediaTek-based Kobo Clara BW and
is packaged separately from NickelRefresh.

- [Kobo USB ADB investigation and technical details](docs/kobo-adb-investigation.md)

### Ko Boy

Ko Boy is an experimental Game Boy and Game Boy Color player based on
Gambatte. The current work explores emulation performance, MediaTek e-paper
presentation, touch controls, and recovery from player crashes.

Its components range from a headless feasibility benchmark to a ROM-driven
display prototype and supervisor. Ko Boy remains a developer prototype rather
than an installable end-user package; no ROMs are included.

[Ko Boy port plan and technical details](docs/gambatte-kobo-port-plan.md)

## Building

The top-level Makefile exposes separate targets for each Speriment. Run
`make help` for the current build and packaging commands. GitHub Actions also
builds each package through its own project-specific workflow.

Release packages use project-specific filenames so their contents are clear.
Kobo installation archives must be renamed to `KoboRoot.tgz` before being
placed in the device's `.kobo` directory.
