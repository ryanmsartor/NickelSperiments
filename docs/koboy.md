# Ko Boy

Ko Boy is the experimental Kobo Game Boy and Game Boy Color frontend in this
repository. It is currently deployed as two adjacent executables: the player
and a supervisor which owns framebuffer backup, crash recovery, and final
restoration. Nickel remains running while Ko Boy exclusively owns touch input.

## Device layout

Install both ARM binaries together:

```text
/mnt/onboard/.adds/koboy/koboy
/mnt/onboard/.adds/koboy/koboy-supervisor
```

ROMs belong in system folders immediately below `/mnt/onboard/KoBoy/Roms/`.
On first use Ko Boy creates Game Boy, Game Boy Color, Game Boy Advance, and
Mega Duck folders. The system browser is otherwise dynamic: adding, renaming,
or removing a top-level folder changes the systems shown without Ko Boy
recreating intentionally removed folders. Games live directly inside their
system folder. Automatic save states are kept in `/mnt/onboard/KoBoy/States/`
with the same system folders as the ROM library. Battery-backed game saves use
the same mirrored layout under `/mnt/onboard/KoBoy/Saves/`. For example,
`Roms/Game Boy/Tetris.gb` maps to `States/Game Boy/Tetris.gb.state0` and
`Saves/Game Boy/Tetris.gb.sav`.

Run the supervisor without a ROM to open the game browser:

```sh
/mnt/onboard/.adds/koboy/koboy-supervisor
```

Passing a ROM path bypasses the startup browser for development and testing.
Local compilation, including the ARM64-hosted NickelTC arrangement, is
documented in [Local Kobo toolchain](local-toolchain.md).

## NickelMenu launcher

Add this entry to `/mnt/onboard/.adds/nm/config`:

```text
menu_item :main :Ko Boy :cmd_spawn :quiet:exec /mnt/onboard/.adds/koboy/koboy-supervisor >>/mnt/onboard/.adds/koboy/koboy.log 2>&1
```

NickelMenu monitors its configuration directory and reloads changes. Keep
backups outside `.adds/nm/`, because NickelMenu parses every file in that
directory as configuration.

## Player flow

The startup browser first shows up to seven system folders per page, followed
by the selected system's alphabetically sorted games. An empty system reports
that no games were found and retains a Back action to the system browser.
During
play, the Menu button pauses emulation and offers Resume Game, Save State,
Load State, Reset Game, Select Game, and Exit to Nickel. Reset Game restarts
the emulated system without reading or writing any state files. The manual
entries use each game's `state1` slot, independently of its automatic `state0`
resume slot.
Selecting another game saves the current automatic state and SRAM, then clears
the browser to a loading screen before loading the selected game's saves. The
first actual game frame is then presented with waited full GC16 so the loading
text cannot bleed into the title screen. Manual Save State, game switching,
and normal exit atomically flush battery-backed SRAM to the game's `.sav`
file. Exit also saves the current automatic state before the supervisor
restores the original framebuffer and returns control to Nickel.
The default 6x integer scale produces a 960-by-864 game viewport on the Clara
BW; `--scale 5` can be used when a smaller viewport is preferred.

For the implementation history, device measurements, and remaining work, see
the [Ko Boy port plan](koboy-port-plan.md).

## Emulator core boundary

Ko Boy's frontend depends on a small core-neutral interface for ROM loading,
frame production, input, reset, save states, and battery-backed save RAM. Core
types stay out of the display, touch, menu, and lifecycle code, and the player
obtains its backend through a factory.

armGBA is pinned as the `deps/armgba` submodule at revision `6deb8f84`. The
`ArmGbaCore` adapter implements
the same interface through armGBA's libretro API, including dynamic GB/GBA
video formats, input, reset, states, and save RAM. The adapter and armGBA's
ARMv7/NEON/JIT core compile with NickelTC; the remaining libretro support
sources also compile except optional CPU LCD effects. That source requires a
newer compiler's `__builtin_convertvector`; Ko Boy does its own display
processing, so `armgba_lcd_compat.c` supplies a no-effects pass-through layer
rather than modifying the pinned dependency. The embedded Gambatte and archive
dependencies now compile, and `make armgba-probe` links a standalone ARM test
executable. Ko Boy now links this complete backend and obtains it through the
core factory. An on-device probe and player test remain required before this
integration is ready for release.

## Refresh scheduling

Gameplay uses only A2 and full GC16. A2 targets a steady 10 Hz cadence across
the game viewport. Once per second, Ko Boy selects a 32-pixel-aligned rectangle
covering accumulated frame changes and starts a full-waveform GC16 cleanup for
that region. While GC16 owns that rectangle, the newest A2 frame continues in
up to four non-overlapping strips around it. Framebuffer writes follow the same
partition so an active GC16 region is not modified underneath the controller.

This replaces the former developer-configurable fast, slow, and slowest tiers.
Menus, loading, wake, and exit retain full-screen GC16 boundaries.
Removing the developer flag hides the menu on the next launch but preserves
the selected configuration. Delete `refresh.conf` to restore the defaults:
A2 at 10.5 Hz, DU at 2 Hz for the slow tier, and full-update GC16 for every
fifth slow refresh.

Settings updates are synchronized through `refresh.conf.tmp`, while the prior
valid configuration is retained as `refresh.conf.bak`. At startup Ko Boy uses
the first complete, valid file in that order: `refresh.conf`, the temporary
file, then the backup. This permits recovery if the Kobo resets while writing
its FAT storage.

Immediately before a refresh configuration becomes active, Ko Boy appends it
to `/mnt/onboard/KoBoy/refresh-active.log` and synchronizes the entry to disk.
After a watchdog reset, the final complete line identifies the refresh types
and frequencies that were active. The log uses monotonic uptime rather than a
wall-clock timestamp because the Kobo clock may not yet be synchronized.

Create `/mnt/onboard/KoBoy/diagnostics` to enable durable refresh diagnostics.
This is independent of `developer`: the experimental settings menu can remain
available without adding gameplay logging. Release-style operation without the
diagnostics flag avoids the additional VFAT writes, log growth, sysfs reads,
and diagnostic timing changes.

Entering and leaving the menu uses waited, full-screen full-update GC16 so the
game and menu do not bleed into one another. Redraws within Refresh Settings
use waited, full-screen partial GC16 and never apply the selected gameplay
waveform inside the menu. Input accumulated during an update is drained, and
all gameplay and UI actions remain blocked until every existing contact has
been released. Exit to Nickel likewise waits for release before giving Nickel
the touchscreen. With diagnostics enabled, each menu update writes `begin` and
`end` records, including its marker and elapsed time, to `refresh-active.log`.
If a wait lasts at least 4.5 seconds—close to HWTCON's five-second timeout—Ko
Boy closes the refresh settings instead of submitting another menu update.

Ko Boy leaves Nickel responsible for interpreting the sleep cover and power
button and for performing system power management. The companion
`libkoboylifecycle.so` NickelHook plugin observes Nickel's actual
`aboutToSleep` and `resumedFromSleep` lifecycle transitions and signals the
active player through `/tmp/koboy.pid`. A sleep transition pauses emulation
immediately. After Nickel finishes waking, Ko Boy redraws the complete game
viewport and controller panel and presents the whole screen with full-update
GC16 before emulation continues. The sleep interval does not consume a
configured timed test run or cause refresh catch-up bursts.

Install the lifecycle bridge at
`/usr/local/Kobo/imageformats/libkoboylifecycle.so` and restart the Kobo so
Nickel loads it. It is intentionally separate from the player process and is a
no-op whenever `/tmp/koboy.pid` is absent. Creating
`/mnt/onboard/.adds/koboy/uninstall-lifecycle-hook` requests removal through
NickelHook's failsafe mechanism.

Before opening any menu, Ko Boy waits for the most recently submitted gameplay
update to finish. Touches accumulated while draining that update are discarded,
so repeated Menu presses cannot enqueue multiple menu transitions. Drain start,
completion, marker, and elapsed time are included in the diagnostic log; a
near-timeout cancels the menu opening.

An accepted Menu press first writes a minimal, metrics-free `menu_press`
breadcrumb. During gameplay, a heartbeat is written every five seconds with
the latest submitted and known-completed markers, the most recent waited
duration, cumulative counts for each waveform, total CPU utilization, and Ko
Boy's CPU utilization. CPU percentages are calculated from `/proc/stat` and
`/proc/self/stat` deltas over the heartbeat interval rather than inferred from
clock frequency. Together these distinguish
a failure in input handling or diagnostic collection from an HWTCON update
which was submitted earlier and never completed.

Every slow and slowest gameplay update also receives durable
`gameplay_refresh begin` and `end` records containing its marker, waveform, and
elapsed time. To observe the fast path without synchronizing VFAT on every
frame, Ko Boy records the same pair for every fiftieth fast update. An unmatched
`begin` after a watchdog reset identifies the request which entered HWTCON and
did not return.

Active-settings records and both sides of each menu refresh also capture
available system memory, Ko Boy's resident memory, CPU0's current clock, and
the CPU, battery, and application-processor thermal-zone readings. Memory is
reported in KiB, the clock in kHz, and temperatures in thousandths of a degree
Celsius. A zero indicates that the corresponding kernel interface could not
be read.

Every gameplay refresh—including fast A2 and DU—waits for completion before
another update is submitted. Slow and slowest updates therefore cannot collide
with a preceding fast update, and fast updates remain blocked until a slower
waveform completes. If a waited update exceeds its target cadence, Ko Boy
advances the scheduling baseline instead of issuing catch-up bursts.

## Display isolation

Create `/mnt/onboard/KoBoy/isolate-no-gameplay-refresh` to diagnose hard
resets. When a ROM is supplied directly, Ko Boy continues emulation, touch
polling, scaling, timing, and framebuffer writes without issuing any HWTCON
update, including during startup and the first game frame. Menu opening is
disabled in this mode so it cannot introduce a display ioctl. The game browser
still requires display updates, so bypass it for a strict zero-HWTCON test.
Remove the flag afterward to restore normal display behavior.

Add `/mnt/onboard/KoBoy/isolate-no-touch` to the display-isolation flag to
leave the touchscreen unopened and ungrabbed while the rest of the player
continues. This distinguishes touchscreen input from supervisor, Nickel, and
framebuffer interactions. Direct ROM launch is required because neither the
game browser nor in-game controls can be operated in this mode.

Ko Boy leaves Nickel running. It exclusively grabs the touchscreen while active,
saves the framebuffer before drawing, and restores it before returning to
Nickel. Stopping Nickel is intentionally avoided: device testing showed that a
`SIGSTOP` lasting roughly one watchdog interval can reboot the Kobo.

## Targeted cleanup refreshes

The initial implementation uses aligned rectangular damage accumulated from
scaled frame differences. It is intentionally conservative while device tests
establish the controller's behavior for simultaneous non-overlapping A2 and
GC16 regions.

The framebuffer cannot reveal which physical e-ink pixels still retain a
visible prior image; it contains only the image Ko Boy intended to display.
Instead, Ko Boy can estimate a per-region "ghosting debt" from its frame
history. Divide the scaled game viewport into tiles and increase each tile's
debt according to the number and magnitude of pixel changes, repeated dark-to-
light transitions, time since its last cleanup, and the waveform used for that
cleanup. Stronger waveforms should reduce more debt than weaker ones.

When a slow refresh becomes due, select the highest-debt neighboring tiles and
combine them into a rectangular HWTCON update. HWTCON accepts rectangles rather
than arbitrary sets of pixels, so selection should balance covering high-debt
tiles against refreshing unchanged area. A slowest refresh can target a larger
high-debt region or advance through a rolling sweep of the viewport.

Initial implementation should retain an occasional full-viewport slowest
refresh as a safety net. Its interval can be increased only after device tests
show that targeted refreshes do not permit artifacts to accumulate over long
sessions. Developer diagnostics should record the selected rectangle, its debt,
and the waveform so the heuristic can be tuned from observed results.

The next refinement is tile-level debt weighting so cleanup can prioritize the
worst subregion instead of taking the bounding rectangle of all recent motion.
Device testing must also confirm minimum dimensions, alignment requirements,
edge artifacts, and whether waveforms spill beyond their requested rectangles.

## Deferred work

- Implement the ability to load ROMs directly from ZIP archives.
- Subfolder handling in the game library.
- Add (max) emulation speed setting.
- Core options config, whether in-UI or just by editing a text file.
- Bluetooth audio?
- Bluetooth controllers?
- Can Ko Boy become a libretro frontend and leverage existing cores for broader console support?
- Update build scripts to call it by proper name, and output the full user-installable package.

beyond koboy:
- Whole LEGAL.md in the repo.
- Special thanks section to upstream projects and contributors whose work has enabled or inspired Ko Boy.
