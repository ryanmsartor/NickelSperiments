# Ko Boy implementation and release plan

## Scope

Ko Boy is an e-ink-focused player for Game Boy, Game Boy Color, Game Boy
Advance, and Mega Duck software on Kobo e-readers. The current implementation
targets the MediaTek framebuffer and touchscreen interfaces of the Kobo Clara
BW. Support for other models must be established through explicit device
testing rather than assumed from screen dimensions alone.

The project is optimized for turn-based games, puzzle games, and slower
adventures. Emulation runs at the console's normal internal rate while the
display presents the latest completed frame at an e-ink-appropriate cadence.
Fast action games remain limited by the physical display even when emulation
itself is full speed.

Ko Boy does not include ROMs, console firmware, or Kobo firmware.

## Current architecture

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
 A2 gameplay updates + targeted DU/GC16 cleanup
          |
 MediaTek framebuffer and HWTCON update interface
```

The frontend talks to the emulator through a core-neutral interface covering
ROM loading, video, input, reset, save states, and battery-backed save RAM.
armGBA is pinned as a submodule and supplies all currently supported systems.
Its GB/GBC implementation contains Gambatte-derived code; Ko Boy no longer has
a separate Gambatte dependency or adapter.

The player and supervisor are separate processes. The supervisor retains a
complete framebuffer backup and restores Nickel's screen after normal exit,
handled signals, or player failure. Nickel continues running while Ko Boy
owns the touchscreen, avoiding the watchdog resets observed when Nickel was
stopped for extended periods.

A NickelHook lifecycle bridge observes Nickel's real sleep and wake events.
It pauses the active player for cover- and button-initiated sleep, then asks
Ko Boy to redraw the complete viewport and controls before emulation resumes.
The same plugin adds a Ko Boy launcher to Nickel's More screen, so an
installation does not depend on NickelMenu.

## Implemented user flow

- Startup presents a dynamic list of system folders below `KoBoy/Roms/`, then
  the games in the chosen folder.
- The default folders are Game Boy, Game Boy Color, Game Boy Advance, and Mega
  Duck; users may add, rename, or remove top-level system folders.
- Saves and states mirror the ROM system-folder layout below `KoBoy/Saves/`
  and `KoBoy/States/`.
- The in-game menu provides Resume, Save State, Load State, Reset, Select Game,
  Display Settings, and Exit to Nickel.
- Automatic resume state and manual state occupy separate slots. Reset starts
  the game from power-on state without loading either slot.
- Touches are drained at game/menu/Nickel boundaries so one contact cannot
  activate controls on both sides of a transition.
- GBA games receive additional L and R touchscreen controls.
- Normal exit saves automatic state and SRAM, then performs a waited full GC16
  before the supervisor restores Nickel's saved framebuffer.

## Display strategy

Gameplay uses steady A2 presentation plus a selectable cleanup policy:

- Adaptive targets high-debt 24-by-24 physical-pixel cells with DU or GC16.
- Intermediate batches adjacent dirty cells at a predictable 2 Hz cadence.
- Aggressive cleans one bounding rectangle at 1 Hz.
- Lazy uses only A2 except for an optional periodic full-screen refresh.

The frontend classifies intended grayscale transitions and tracks approximate
ghosting debt because the framebuffer cannot report residual physical pigment
state. A waited-update discipline prevents overlapping framebuffer writes and
waveform requests over the same display state. Menus, loading boundaries,
wake, and exit use waited full-screen GC16 where a clean visual boundary is
more important than uninterrupted animation.

Display settings currently apply globally. Per-game profiles remain optional
future work.

## Device evidence

Development tests on a Kobo Clara BW established the following:

- The original GB feasibility probe ran far above real time with save-state
  and save-RAM round trips succeeding.
- The MediaTek framebuffer path sustained latest-frame presentation without
  accumulating a queue of stale frames.
- Multitouch can hold a direction while pressing a face button after applying
  the Clara BW's coordinate transform.
- The supervisor restored Nickel after both normal player exit and a forced
  player SIGKILL.
- Stopping Nickel for approximately one watchdog interval can reboot the
  reader; the current design therefore leaves Nickel running.
- GB and Mega Duck gameplay have passed manual testing. GBA reaches gameplay,
  but requires broader compatibility, performance, save, and long-session
  stability testing before the first public release.

Historical synthetic display and isolation binaries remain developer tools;
they are not intended for the end-user package.

## Licensing and source distribution

Ko Boy and armGBA are distributed under GPL version 2. armGBA's bundled GB/GBC
backend contains Gambatte-derived GPLv2 code. Ko Boy implements the minimal
framebuffer and input integration directly and does not link FBInk.

The release bundle must include `LICENSES/KOBOY-LICENSES.md` and every license
file it links. Corresponding source, build scripts, and exact pinned submodule
revisions must remain available for each released binary. A formal legal
review may still be appropriate before broad distribution.

## Work required for the first release

### Install and removal

- Produce `Ko-Boy-KoboRoot.tgz` with the player, supervisor, combined
  launcher/lifecycle plugin, and license bundle.
- Keep executable files under the project installation directory and user
  content under `/mnt/onboard/KoBoy/`.
- Make installation and upgrades idempotent.
- Provide a reversible uninstaller which removes only Ko Boy program files and
  launcher integration, never ROMs, saves, states, or user configuration.
- Verify that firmware upgrades and package removal leave Nickel bootable.

### Build and publication

- Add a Ko Boy package target and have its dedicated workflow upload the
  project-specific KoboRoot archive rather than loose development binaries.
- Validate archive ownership, permissions, paths, ARM architecture, license
  contents, and pinned source revisions in CI.
- Publish source availability, installation instructions, supported-device
  scope, known limitations, and artifact hashes with the release.

### Qualification

- Test clean install, upgrade, reinstall, and uninstall on the Clara BW.
- Test NickelMenu launch, shell recovery launch, clean exit, player crash,
  sleep/wake, and repeated game/menu transitions.
- Exercise representative GB, GBC, GBA, and Mega Duck games through SRAM,
  automatic state, manual state, reset, game switching, and long sessions.
- Give GBA additional performance and stability coverage.
- Confirm that diagnostics remain disabled unless explicitly requested and
  that normal logs cannot grow without bound.

## Known limitations and deferred work

- No audio output.
- No Bluetooth controller support.
- No ZIP loading or nested ROM folders.
- Display settings are global rather than per game.
- Action games are constrained by e-ink latency and touch controls.
- Compatibility outside the Clara BW is unverified.
- Killing the supervisor itself or losing power during a framebuffer update is
  outside the player-crash recovery guarantee.
