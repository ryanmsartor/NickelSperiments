# Ko Boy

Ko Boy is a Kobo Game Boy, Game Boy Color, Game Boy Advance, and Mega Duck player built around armGBA. It is currently deployed as two adjacent executables: the player and a supervisor which owns framebuffer backup, crash recovery, and final restoration. Nickel remains running while Ko Boy exclusively owns touch input.

## Device layout

Install both ARM binaries together:

```text
/mnt/onboard/.adds/koboy/koboy
/mnt/onboard/.adds/koboy/koboy-supervisor
```

ROMs belong in system folders immediately below `/mnt/onboard/KoBoy/Roms/`. On first use Ko Boy creates Game Boy, Game Boy Color, Game Boy Advance, and Mega Duck folders. The system browser is otherwise dynamic: adding, renaming, or removing a top-level folder changes the systems shown without Ko Boy recreating intentionally removed folders. Games live directly inside their system folder. Automatic save states are kept in `/mnt/onboard/KoBoy/States/` with the same system folders as the ROM library. Battery-backed game saves use the same mirrored layout under `/mnt/onboard/KoBoy/Saves/`. For example, `Roms/Game Boy/Tetris.gb` maps to `States/Game Boy/Tetris.gb.state0` and `Saves/Game Boy/Tetris.gb.sav`.

Run the supervisor without a ROM to open the game browser:

```sh
/mnt/onboard/.adds/koboy/koboy-supervisor
```

Passing a ROM path bypasses the startup browser for development and testing. Local compilation, including the ARM64-hosted NickelTC arrangement, is documented in [Local Kobo toolchain](local-toolchain.md).

## Nickel launcher

The combined launcher/lifecycle plugin adds a **Ko Boy** row to Nickel's **More** screen near Help, using the same stock anchor as NickelModManager. Tapping it starts the supervisor asynchronously and writes player output to `/mnt/onboard/.adds/koboy/koboy.log`. A running player or a supervisor which is still starting prevents a second launch. Other mods may affect the relative order of rows which use that anchor, but do not replace the Ko Boy entry.

This launcher is built into Ko Boy and does not require NickelMenu. Users who already have NickelMenu may still create their own entry, but doing so produces a second route to the same supervisor and is unnecessary.

## Player flow

The startup browser first shows up to seven system folders per page, followed by the selected system's alphabetically sorted games. An empty system reports that no games were found and retains a Back action to the system browser. During play, the Menu button pauses emulation and offers Resume Game, Save State, Load State, Reset Game, Select Game, Display Settings, and Exit to Nickel. Reset Game restarts the emulated system without reading or writing any state files. The manual entries use each game's `state1` slot,  ndependently of its automatic `state0` resume slot. Selecting another game saves the current automatic state and SRAM, then clears the browser to a loading screen before loading the selected game's saves. The first actual game frame is then presented with waited full GC16 so the loading text cannot bleed into the title screen. Manual Save State, game switching, and normal exit atomically flush battery-backed SRAM to the game's `.sav` file. Exit also saves the current automatic state before the supervisor restores the original framebuffer and returns control to Nickel. The default 6x integer scale produces a 960-by-864 game viewport on the Clara BW; `--scale 5` can be used when a smaller viewport is preferred.

For the implementation history, device measurements, and remaining work, see the [Ko Boy port plan](koboy-port-plan.md).

## Emulator core boundary

Ko Boy's frontend depends on a small core-neutral interface for ROM loading, frame production, input, reset, save states, and battery-backed save RAM. Core types stay out of the display, touch, menu, and lifecycle code, and the player obtains its backend through a factory.

armGBA is pinned as the `deps/armgba` submodule at revision `6deb8f84`. The `ArmGbaCore` adapter implements the same interface through armGBA's libretro API, including dynamic GB/GBA video formats, input, reset, states, and save RAM. The adapter and armGBA's ARMv7/NEON/JIT core compile with NickelTC; the remaining libretro support sources also compile except optional CPU LCD effects. That source requires a newer compiler's `__builtin_convertvector`; Ko Boy does its own display processing, so `armgba_lcd_compat.c` supplies a no-effects pass-through layer rather than modifying the pinned dependency. armGBA's bundled GB/GBC backend and archive dependencies compile as part of the player. Ko Boy links the complete backend and obtains it through the core factory. GB, Mega Duck, and GBA games have all reached on-device execution; broader GBC/GBA compatibility and long-session GBA stability remain release-validation work.

## Refresh scheduling

Gameplay uses regularly paced A2 updates plus one of four global cleanup policies. The default target is 10 Hz; developer mode also offers 10.25, 10.5, 10.75, 11, 11.25, and 11.5 Hz, plus Unlimited. Settings persist in `KoBoy/display.conf` and currently apply to every game.

Progressive measures frame-change debt independently in fixed 24-by-24 framebuffer cells. On each display tick it may start full-waveform GC16 or DU cleanups for up to eight highest-debt contiguous rectangles of dirty cells, with at most thirty-two cleanups active at once. This staggers cleanup starts instead of blinking a whole batch simultaneously. While a cleanup owns those cells, the newest A2 frame continues through coalesced rectangles covering the remainder. Framebuffer writes follow the same partition so active cleanup cells are not modified underneath the controller.

Moderate issues up to ten GC16 or DU cleanups across the highest-debt contiguous rectangles at 2 Hz, combining adjacent dirty 24-by-24 cells. It uses GC16 for gray destinations and DU for gray-to-black/white transitions.

Aggressive considers only transitions involving gray, splits their affected cells into at most two area-minimizing aligned bounding rectangles, and issues GC16 at 1 Hz. Pure black-to-white and white-to-black motion remains A2-only and does not create or expand those rectangles. Moderate applies the same A2-only exclusion and uses separate GC16 and DU cleanups; Aggressive remains intentionally broader and GC16-only.

Lazy is the default and schedules only the paced A2 updates. It performs no regional cleanup, so any cleanup beyond A2 comes exclusively from Full Refresh Frequency.

Full Refresh Frequency can be Off, 5, 15, 30, 60, or 90 seconds and defaults to 60 seconds. When due, the full-screen GC16 drains active cleanups and replaces that tick's policy cleanup. Grid Effect draws a one-pixel white divider between logical emulated pixels before output reaches the framebuffer and defaults to Off. Bichrome flattens the scaled image to absolute black and white, with fixed luminance splits of 25%, 50%, or 75%, plus Dithered and Off. Dithered is the default and uses a stable 4-by-4 ordered pattern of physical panel pixels, varying black-dot density to represent intermediate shades without temporal noise. Menus, loading, wake, and exit retain full-screen GC16 boundaries.


| Behavior | Progressive | Lazy | Moderate | Aggressive |
|---|---|---|---|---|
| Normal display | Paced A2 | Paced A2 | Paced A2 | Paced A2 |
| Cleanup cell size | 24×24 physical pixels | None | 24×24 physical pixels | Tracks 24×24 cells, then bounds eligible cells |
| Cleanup cadence | Every display tick | Never | 2 Hz | 1 Hz |
| Maximum starts per cleanup opportunity | 8 rectangles | 0 | 10 total GC16/DU rectangles | 2 rectangles |
| Maximum concurrently active | 32 | 0 | 20, though the 0.5-second cadence normally lets the previous batch finish | 2 in normal operation |
| Rectangle construction | Combines adjacent eligible cells | None | Combines adjacent eligible cells | Up to two area-minimizing bounding rectangles |
| Cleanup waveforms | GC16 and DU | None | GC16 and DU | GC16 only |
| Gray destination | GC16 debt | Ignored until full refresh | GC16 cleanup debt | Included |
| Gray → black/white | DU debt | Ignored until full refresh | DU cleanup debt | Included |
| Black ↔ white motion | Low aging debt; eventually DU | A2-only | Ignored; remains A2-only | Ignored; remains A2-only |
| GC16 stability gate | Requires 2 unchanged display snapshots | None | None | N/A |
| GC16 emergency override | After debt equivalent to 4 full-range cell transitions | N/A | N/A | N/A |
| Black/white aging threshold | DU after 8 cell-equivalents | Not used | Not used | Not used |
| Primary goal | Targeted quality with limited flashing | Maximum smoothness between full refreshes | Predictable, moderate grayscale cleanup | Broad cleanup with smaller affected areas |


Display Settings normally exposes Full Refresh Frequency and Grid Effect. Create `/mnt/onboard/KoBoy/developer` to additionally expose Refresh Type, Bichrome, and Target Refresh Rate. The regional Progressive, Moderate, and Aggressive implementations remain available for experimentation even though Lazy and Dithered are the defaults.

Create `/mnt/onboard/KoBoy/diagnostics` to enable durable refresh diagnostics. Release-style operation without diagnostics avoids the additional VFAT writes, log growth, sysfs reads, and diagnostic timing changes.

Entering and leaving the menu uses waited, full-screen full-update GC16 so the game and menu do not bleed into one another. Transitions between menu screens use waited, full-screen DU over a strictly black-and-white palette and never apply the selected gameplay waveform inside the menu. Input accumulated during an update is drained, and all gameplay and UI actions remain blocked until every existing contact has been released. Exit to Nickel likewise waits for release before giving Nickel the touchscreen. With diagnostics enabled, each menu update writes `begin` and `end` records, including its marker and elapsed time, to `refresh-active.log`. If a wait lasts at least 4.5 seconds—close to HWTCON's five-second timeout—Ko Boy closes Display Settings instead of submitting another menu update.

Ko Boy leaves Nickel responsible for interpreting the sleep cover and power button and for performing system power management. While Ko Boy owns the touchscreen, the companion plugin suppresses Nickel's inactivity-only timeout so Nickel cannot take the visible screen while touches continue going to the emulator. Explicit cover and power-button sleep remain enabled. The plugin observes Nickel's actual `aboutToSleep` and `resumedFromSleep` lifecycle transitions and signals the active player through `/tmp/koboy.pid`. A sleep transition pauses emulation immediately. After Nickel finishes waking, Ko Boy redraws the complete game viewport and controller panel and presents the whole screen with full-update GC16 before emulation continues. The sleep interval does not consume a configured timed test run or cause refresh catch-up bursts.

Install `Ko-Boy-KoboRoot.tgz` by renaming it to `KoboRoot.tgz`, copying it into the Kobo's `.kobo` directory, and safely ejecting the reader. The package installs the player and supervisor under `/mnt/onboard/.adds/koboy`, installs the launcher/lifecycle plugin at `/usr/local/Kobo/imageformats/libkoboylifecycle.so`, creates the four stock ROM directories, and includes the complete license bundle. Reinstalling the archive updates program files without replacing ROMs, saves, states, or settings.

To uninstall, create an empty `/mnt/onboard/.adds/koboy/uninstall` file and reboot. NickelHook removes the loaded plugin safely during startup and deletes only the Ko Boy program directory. User content under `/mnt/onboard/KoBoy` is preserved. The lifecycle hooks are inactive whenever `/tmp/koboy.pid` is absent, while the More-screen launcher remains available whenever the plugin is installed.

Before opening any menu, Ko Boy waits for the most recently submitted gameplay update to finish. Touches accumulated while draining that update are discarded, so repeated Menu presses cannot enqueue multiple menu transitions. Drain start, completion, marker, and elapsed time are included in the diagnostic log; a near-timeout cancels the menu opening.

An accepted Menu press first writes a minimal, metrics-free `menu_press` breadcrumb. During gameplay, a heartbeat is written every five seconds with the latest submitted and known-completed markers, the most recent waited duration, cumulative counts for each waveform, total CPU utilization, and Ko Boy's CPU utilization. CPU percentages are calculated from `/proc/stat` and `/proc/self/stat` deltas over the heartbeat interval rather than inferred from clock frequency. Together these distinguish a failure in input handling or diagnostic collection from an HWTCON update which was submitted earlier and never completed.

Every slow and slowest gameplay update also receives durable `gameplay_refresh begin` and `end` records containing its marker, waveform, and elapsed time. To observe the fast path without synchronizing VFAT on every frame, Ko Boy records the same pair for every fiftieth fast update. An unmatched `begin` after a watchdog reset identifies the request which entered HWTCON and did not return.

Active-settings records and both sides of each menu refresh also capture available system memory, Ko Boy's resident memory, CPU0's current clock, and the CPU, battery, and application-processor thermal-zone readings. Memory is reported in KiB, the clock in kHz, and temperatures in thousandths of a degree Celsius. A zero indicates that the corresponding kernel interface could not be read.

Every gameplay refresh—including fast A2 and DU—waits for completion before another update is submitted. Slow and slowest updates therefore cannot collide with a preceding fast update, and fast updates remain blocked until a slower waveform completes. If a waited update exceeds its target cadence, Ko Boy advances the scheduling baseline instead of issuing catch-up bursts.

## Display isolation

Create `/mnt/onboard/KoBoy/isolate-no-gameplay-refresh` to diagnose hard resets. When a ROM is supplied directly, Ko Boy continues emulation, touch polling, scaling, timing, and framebuffer writes without issuing any HWTCON update, including during startup and the first game frame. Menu opening is disabled in this mode so it cannot introduce a display ioctl. The game browser still requires display updates, so bypass it for a strict zero-HWTCON test. Remove the flag afterward to restore normal display behavior.

Add `/mnt/onboard/KoBoy/isolate-no-touch` to the display-isolation flag to leave the touchscreen unopened and ungrabbed while the rest of the player continues. This distinguishes touchscreen input from supervisor, Nickel, and framebuffer interactions. Direct ROM launch is required because neither the game browser nor in-game controls can be operated in this mode.

Ko Boy leaves Nickel running. It exclusively grabs the touchscreen while active, saves the framebuffer before drawing, and restores it before returning to Nickel. Stopping Nickel is intentionally avoided: device testing showed that a `SIGSTOP` lasting roughly one watchdog interval can reboot the Kobo.

## Targeted cleanup refreshes

Progressive and Moderate accumulate debt in fixed aligned 24-by-24 framebuffer cells. Device tests establish the controller's behavior for simultaneous non-overlapping A2, DU, and GC16 regions at these boundaries.

The framebuffer cannot reveal which physical e-ink pixels still retain a visible prior image; it contains only the image Ko Boy intended to display. Instead, Ko Boy can estimate a per-region "ghosting debt" from its frame history. Divide the scaled game viewport into tiles and increase each tile's debt according to the number and magnitude of pixel changes, repeated dark-to-light transitions, time since its last cleanup, and the waveform used for that cleanup. Stronger waveforms should reduce more debt than weaker ones.

On each display tick, Ko Boy finds the highest-debt rectangles composed entirely of adjacent dirty cells and starts up to eight rectangular HWTCON updates while fewer than thirty-two older cleanups remain active. This lets one update clean a large contiguous changed area without bridging clean gaps merely because they lie inside a global bounding rectangle. Active rectangles cannot be selected again, and cells not selected retain their debt for later cleanup.

Debt is classified by the intended grayscale transition. A destination gray level requires GC16. A transition from gray to pure black or white requires DU. Pure black/white motion under A2, such as a black sprite crossing a white background, accumulates only a low aging charge and does not immediately request cleanup. Eight tile-equivalents of such endpoint motion make a cell eligible for an eventual DU cleanup. GC16 rectangles are selected before DU rectangles because they require accurate grayscale output; either waveform clears the weaker debt it supersedes in the cells it cleans.

GC16 cleanup is also stability-gated. Debt continues accumulating while a cell is changing, but the cell normally becomes eligible only after remaining unchanged for two display snapshots. This lets scrolling finish before GC16 cleans the settled image instead of repeatedly flashing background segments that immediately move again. A debt equivalent to four complete maximum-range tile transitions bypasses the stability gate so sustained motion cannot defer cleanup indefinitely. DU does not use the stability gate because its endpoint repair is less visually disruptive.

Initial implementation should retain an occasional full-viewport slowest refresh as a safety net. Its interval can be increased only after device tests show that targeted refreshes do not permit artifacts to accumulate over long sessions. Developer diagnostics should eventually record each selected cell, its debt, and the waveform so the heuristic can be tuned from observed results. Device testing must also confirm minimum dimensions, alignment requirements, edge artifacts, and whether waveforms spill beyond their requested rectangles.

## Remaining release work

- Validate the complete, reversible KoboRoot installation package on-device.
- Validate install, upgrade, firmware-upgrade, and uninstall behavior without
  removing user ROMs, saves, states, or configuration.
- Exercise representative GB, GBC, GBA, and Mega Duck software in longer
  sessions, with additional focus on GBA stability and save compatibility.
- Publish supported-device scope, known limitations, source availability, and
  acknowledgements with the first release.

## Deferred enhancements

- Loading ROMs directly from ZIP archives.
- Nested folders below each system in the game library.
- Maximum emulation-speed (alternative to fast forward).
- Advanced core-option settings.
    - BIOS selection - armgba native or user-supplied.
    - RTC (Real-Time Clock) emulation settings.
- Per-game display profiles.
- Solar level and tilt mappings for games that need them.
- Bluetooth audio and physical controller support.
- Additional libretro-compatible systems where the hardware and e-ink display
  make them practical.
