# Ko Boy

Ko Boy is an e-ink-focused Game Boy, Game Boy Color, Game Boy Advance, and Mega Duck player for Kobo e-readers, built around armGBA. It combines full-speed emulation with a display scheduler designed for e-ink, touchscreen controls, Bluetooth audio, an on-device game browser, persistent saves and states, suspend handling, and supervised return to Nickel.

Ko Boy currently targets the MediaTek-based Kobo Clara BW. Other models may differ in framebuffer, touchscreen, lifecycle, and audio behavior and are not supported until tested explicitly. Turn-based games, puzzle games, and slower adventures are the best fit; action games remain constrained by e-ink latency and touchscreen controls even when emulation itself runs at full speed.

Ko Boy does not include ROMs, proprietary console firmware, or Kobo firmware.

## Installation and removal

Rename `Ko-Boy-KoboRoot.tgz` to `KoboRoot.tgz`, copy it into the Kobo's `.kobo` directory, and safely eject the reader. The package installs the player and supervisor under `/mnt/onboard/.adds/koboy`, installs its launcher and lifecycle plugin under `/usr/local/Kobo/imageformats`, creates the stock ROM folders, and includes the complete license bundle. Reinstalling updates program files without replacing ROMs, saves, states, BIOS files, or settings.

Ko Boy adds a **Ko Boy** entry to Nickel's **More** screen and does not require NickelMenu. The entry appears near Help, using the same stock anchor as NickelModManager. Mods which use the same anchor may affect relative ordering without replacing the Ko Boy entry. An optional NickelMenu entry would only provide a second route to the same supervisor.

To uninstall, create an empty `/mnt/onboard/.adds/koboy/uninstall` file and reboot. The startup hook safely unloads and removes Ko Boy's plugin and program directory while preserving all user content under `/mnt/onboard/KoBoy`.

## ROM library

Put games in system folders directly below `/mnt/onboard/KoBoy/Roms/`. On first use, Ko Boy creates these folders:

- `Game Boy`
- `Game Boy Color`
- `Game Boy Advance`
- `Mega Duck`

The system browser is dynamic. Adding, renaming, or removing a top-level system folder changes the systems shown; intentionally removed stock folders are not recreated. Games must live directly inside a system folder because nested ROM folders are not currently supported.

Uncompressed ROMs and ZIP archives are supported. A ZIP appears as one game and loads the first supported ROM it contains.

Saves and states mirror the ROM folder structure:

```text
KoBoy/Roms/Game Boy/Tetris.gb
KoBoy/Saves/Game Boy/Tetris.gb.sav
KoBoy/States/Game Boy/Tetris.gb.state0
KoBoy/States/Game Boy/Tetris.gb.state1
```

The `state0` file is Ko Boy's automatic resume state. The `state1` file is controlled by the manual Load State and Save State buttons. ZIP-based games use the archive filename when naming their save and state files.

## Playing games

Launching Ko Boy opens the system browser, followed by the selected system's alphabetically sorted games. An empty system reports that no games were found and retains a Back button. The browser shows up to seven systems or games per page.

The in-game Menu button pauses emulation and opens these controls:

- Resume Game
- Load State and Save State
- Display Settings
- Software volume from 0 through 20
- Reset Game
- Change Game
- Exit to Nickel

Reset Game starts the emulated system from power-on without reading or writing either state slot. Changing games saves the current automatic state and battery-backed save RAM before loading the next game. Manual Save State, game switching, and normal exit also flush save RAM atomically. Exit saves the automatic state, performs a full display cleanup, and restores Nickel's original framebuffer.

GBA games add L and R buttons above the A and B controls. The default 6× integer scale produces a 960-by-864 game viewport on the Clara BW; direct development launches may use `--scale 5` for a smaller viewport.

Input is drained at game, menu, and Nickel boundaries so one touch cannot activate controls on both sides of a transition. Repeated Menu presses cannot queue multiple menu transitions.

## Sleep and wake

Ko Boy leaves Nickel running and relies on its companion plugin to observe Nickel's actual sleep and wake lifecycle. Cover- and power-button-initiated sleep pause emulation immediately. After Nickel finishes waking, Ko Boy redraws the full game viewport and controller panel with a full GC16 refresh before resuming emulation.

While Ko Boy owns the touchscreen, the plugin suppresses Nickel's inactivity-only timeout so Nickel cannot reclaim the screen while touches continue going to the emulator. Explicit cover and power-button sleep remain enabled.

## Bluetooth audio

Pair and connect Bluetooth headphones through Nickel before launching a game. Ko Boy checks the firmware Bluetooth service when a game loads and starts no audio pipeline when an output device is not connected.

The menu volume ranges from 0 to 20, defaults to 10, and persists in `/mnt/onboard/KoBoy/audio.conf`. Volume 0 releases the audio pipeline entirely. Audio currently targets the Clara BW's stock MediaTek GStreamer sink; behavior on other Kobo platforms is unverified.

Internally, armGBA's 32,768 Hz stereo output passes through a bounded, non-blocking queue to one persistent 48 kHz GStreamer pipeline. Emulation never waits for Bluetooth audio; new audio frames are dropped if the consumer falls behind. Queued audio is discarded when a menu opens or the device sleeps.

## BIOS selection

Ko Boy automatically uses recognized BIOS files placed directly in `/mnt/onboard/KoBoy/BIOS/`. If a suitable file is missing or unusable, armGBA's built-in open-source boot ROM is used instead. This behavior is deterministic and has no user setting.

Recognized filenames are:

- `gba_bios.bin` for Game Boy Advance.
- `gb_bios.bin` or `dmg_boot.bin` for Game Boy.
- `gbc_bios.bin` or `cgb_boot.bin` for Game Boy Color.
- `cgb_agb_boot.bin`, `cgb_agb0_boot.bin`, or `agb_boot.bin` for Game Boy games running in the Game Boy Advance model.
- `mgb_boot.bin` for Game Boy Pocket.

The install package creates the BIOS directory but does not distribute proprietary BIOS images.

## Display settings

Gameplay uses steady full-viewport A2 updates at 6.5 Hz by default, dithered bichrome output, and a full-screen GC16 every 60 seconds. Display settings persist globally in `/mnt/onboard/KoBoy/display.conf` rather than per game.

The normal Display Settings menu exposes:

- **Full Refresh Frequency:** Off, 5, 15, 30, 60, or 90 seconds.
- **Grid Effect:** an optional one-pixel white divider between logical emulated pixels.

Creating `/mnt/onboard/KoBoy/developer` additionally exposes:

- **Target Refresh Rate:** 5 through 8 Hz in quarter-Hz increments. The default is 6.5 Hz.

Ko Boy always uses a stable 4-by-4 ordered pattern of physical panel pixels to represent intermediate shades without temporal noise. Menus use a black-and-white palette and waited full-screen updates: GC16 when entering or leaving the menu and DU between menu screens. Returning from the menu first clears the panel through a waited white-screen GC16, then presents the restored game and controls through a second waited GC16. Loading, wake, and exit also use full-screen GC16 where a clean visual boundary matters more than uninterrupted animation.

Each display tick draws the complete latest frame and submits one A2 update. Before submitting the next update, Ko Boy waits for the previous marker to complete. A configured full-screen refresh replaces its scheduled A2 update, and missed cadence deadlines advance the scheduling baseline instead of causing catch-up bursts.

## Diagnostics and recovery

Create `/mnt/onboard/KoBoy/diagnostics` to enable durable refresh and performance diagnostics. Normal release-style operation leaves diagnostics off to avoid extra VFAT writes, sysfs reads, log growth, and timing changes.

Diagnostics record menu and gameplay refresh markers, waveform timing, update counts, memory availability, resident memory, CPU clock and utilization, and available thermal readings. An unmatched refresh `begin` after a reset can identify an update which entered the display driver but never returned. Player output is written to `/mnt/onboard/.adds/koboy/koboy.log`.

Two additional developer flags isolate hard-reset causes. A strict test requires launching a ROM directly rather than using the game browser:

- `/mnt/onboard/KoBoy/isolate-no-gameplay-refresh` retains emulation, scaling, touch polling, timing, and framebuffer writes without submitting HWTCON gameplay updates. Menu opening is disabled.
- `/mnt/onboard/KoBoy/isolate-no-touch`, used with display isolation, leaves the touchscreen unopened and ungrabbed.

The supervisor holds a complete framebuffer backup and restores Nickel after normal exit, handled signals, or player failure. It cannot recover from its own termination or from power loss during a framebuffer update.

## Architecture

```text
ROM + SRAM + save states
          |
          v
  armGBA/libretro backend
          |
          v
 latest completed video frame
          |
 scaling + grayscale classification + touch overlay
          |
 full-viewport A2 presentation + periodic GC16
          |
 MediaTek framebuffer and HWTCON interface
```

The frontend uses a core-neutral interface for ROM loading, video, input, reset, states, and battery-backed save RAM. armGBA is pinned as the `deps/armgba` submodule and supplies every current system. Its GB/GBC implementation contains Gambatte-derived code, but Ko Boy has no separate Gambatte dependency or adapter.

The `ArmGbaCore` adapter connects that interface to armGBA's libretro API, including dynamic GB/GBA video formats, input, reset, states, save RAM, and ZIP support. armGBA's optional CPU LCD effects require a compiler intrinsic unavailable in NickelTC, so Ko Boy supplies a no-effects compatibility layer and performs display processing in its own frontend.

The player and supervisor are separate processes. Nickel remains running while Ko Boy exclusively grabs the touchscreen, avoiding the watchdog resets observed when Nickel was stopped for extended periods. The NickelHook-based plugin provides the More-screen launcher and lifecycle signals.

Local compilation and the ARM64-hosted NickelTC arrangement are documented in [Local Kobo toolchain](local-toolchain.md). Developers can open the browser from a shell with:

```sh
/mnt/onboard/.adds/koboy/koboy-supervisor
```

Passing a ROM path to the supervisor bypasses the browser for development and isolation testing.

## Validation status

Testing on the Kobo Clara BW has established:

- GB and Mega Duck gameplay, save-state, and save-RAM round trips.
- GBA execution and Bluetooth audio, with broader compatibility and long-session stability still needing coverage.
- Latest-frame presentation without accumulating stale display frames.
- Multitouch direction and face-button combinations using the Clara BW coordinate transform.
- Framebuffer restoration after normal exit and forced player termination.
- Cover and power-button sleep, wake redraw, and controller-panel restoration.
- Package installation and reversible removal while preserving user content.

Stopping Nickel for approximately one watchdog interval can reboot the reader, so the current design deliberately keeps Nickel running.

## Known limitations

- Compatibility outside the Kobo Clara BW is unverified.
- GBA needs broader performance, save, compatibility, and long-session testing.
- Bluetooth audio support is specific to the tested MediaTek firmware stack.
- ZIP archives load only the first supported ROM they contain.
- Nested ROM folders are not supported.
- Display settings are global rather than per game.
- There is no Bluetooth or other physical-controller support.
- E-ink latency limits fast action games regardless of emulation speed.

## Release qualification remaining

- Validate the dedicated Ko Boy workflow and aggregate release upload.
- Verify archive ownership, permissions, paths, architecture, license contents, pinned sources, and published hashes in CI.
- Repeat clean install, upgrade, reinstall, and uninstall testing from a release candidate.
- Exercise representative GB, GBC, GBA, and Mega Duck games through SRAM, automatic state, manual state, reset, game switching, sleep/wake, and long sessions.
- Confirm that diagnostics remain opt-in and ordinary logs cannot grow without bound.
- Publish installation instructions, supported-device scope, source availability, and known limitations with the release.

Ko Boy and armGBA are distributed under GPL version 2. The release bundle must include `LICENSES/KOBOY/KOBOY-LICENSES.md` and every license file it links. Corresponding source, build scripts, and exact pinned submodule revisions must remain available for each released binary. A formal legal review may still be appropriate before broad distribution.

## Deferred enhancements

- Nested game-library folders.
- Maximum emulation speed as an alternative to fast forward.
- Advanced core options, including RTC settings.
- Per-game display profiles.
- Solar-level and tilt mappings for games that require them.
- Physical controller support.
- Additional libretro-compatible systems suited to the hardware and display.
