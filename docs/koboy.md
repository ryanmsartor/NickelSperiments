# Ko Boy

Ko Boy is the experimental Kobo Game Boy and Game Boy Color frontend in this
repository. It is currently deployed as two adjacent executables: the player
and a supervisor which owns Nickel suspension, framebuffer backup, crash
recovery, and final restoration.

## Device layout

Install both ARM binaries together:

```text
/mnt/onboard/.adds/koboy/kobo-gambatte-display
/mnt/onboard/.adds/koboy/kobo-gambatte-supervisor
```

ROMs belong in `/mnt/onboard/KoBoy/Roms/`. Ko Boy scans that directory for
case-insensitive `.gb` and `.gbc` extensions. Automatic save states are kept
in `/mnt/onboard/KoBoy/States/` and keyed by ROM filename.

Run the supervisor without a ROM to open the game browser:

```sh
/mnt/onboard/.adds/koboy/kobo-gambatte-supervisor
```

Passing a ROM path bypasses the startup browser for development and testing.
Local compilation, including the ARM64-hosted NickelTC arrangement, is
documented in [Local Kobo toolchain](local-toolchain.md).

## NickelMenu launcher

Add this entry to `/mnt/onboard/.adds/nm/config`:

```text
menu_item :main :Ko Boy :cmd_spawn :quiet:exec /mnt/onboard/.adds/koboy/kobo-gambatte-supervisor >>/mnt/onboard/.adds/koboy/koboy.log 2>&1
```

NickelMenu monitors its configuration directory and reloads changes. Keep
backups outside `.adds/nm/`, because NickelMenu parses every file in that
directory as configuration.

## Player flow

The startup browser shows seven alphabetically sorted games per page. During
play, the Menu button pauses emulation and offers Resume Game, Select Game,
and Exit to Nickel. Selecting another game saves the current automatic state
and clears the browser to a loading screen before loading the selected game's
state. The first actual game frame is then presented with waited full GC16 so
the loading text cannot bleed into the title screen. Exit also saves the current state before the supervisor restores the
original framebuffer and resumes Nickel. During play, Ko Boy performs a
waited, non-flashing partial GC16 cleanup every second by default. This uses
GC16's higher-fidelity waveform without the disorienting inversion caused by
the driver's full-update mode.
The default 6x integer scale produces a 960-by-864 game viewport on the Clara
BW; `--scale 5` can be used when a smaller viewport is preferred.

For the implementation history, device measurements, and remaining work, see
the [Ko Boy port plan](gambatte-kobo-port-plan.md).

## Developer refresh settings

Create `/mnt/onboard/KoBoy/developer` before launching Ko Boy to add Refresh
Settings to the in-game menu. The menu exposes five persistent controls:

- Fast refresh type: A2, DU, or GL16.
- Slow refresh type: A2, DU, GL16, or GC16 Partial.
- Slowest refresh type: GL16, GC16 Partial, or GC16 Full Flash.
- Fast refresh frequency: 6, 8, 10, 10.5, 11, 11.5, or 12 Hz.
- Slow refresh frequency: Off, 0.1, 0.2, 0.5, 1, or 2 Hz.

Every fifth scheduled slow refresh uses the slowest type instead. It therefore
shares the slow frequency rather than having a separate frequency control.

Tap a row to advance to its next value. Changes are written immediately to
`/mnt/onboard/KoBoy/refresh.conf` and take effect when returning to the game.
Removing the developer flag hides the menu on the next launch but preserves
the selected configuration. Delete `refresh.conf` to restore the defaults:
A2 at 8 Hz with partial GC16 at 1 Hz for both slow tiers.

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

The menu itself uses waited, full-screen partial GC16 updates; changing a
setting never applies the selected gameplay waveform inside the menu. Input
events accumulated during an update are drained, and a new action requires a
touch release followed by a fresh press. With diagnostics enabled, each menu
update writes `begin` and `end` records, including its marker and elapsed time,
to `refresh-active.log`. If a wait lasts at least 4.5 seconds—close to
HWTCON's five-second timeout—Ko Boy closes the refresh settings instead of
submitting another menu update.

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
