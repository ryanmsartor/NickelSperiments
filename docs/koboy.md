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
Settings to the in-game menu. The menu exposes four persistent controls:

- Fast refresh type: A2, DU, GL16, GC16 Partial, or GC16 Full Flash.
- Slow refresh type: the same five waveform/update combinations.
- Fast refresh frequency: 2, 4, 6, 8, or 10 Hz.
- Slow refresh frequency: Off, 0.1, 0.2, 0.5, 1, or 2 Hz.

Tap a row to advance to its next value. Changes are written immediately to
`/mnt/onboard/KoBoy/refresh.conf` and take effect when returning to the game.
Removing the developer flag hides the menu on the next launch but preserves
the selected configuration. Delete `refresh.conf` to restore the defaults:
A2 at 8 Hz with partial GC16 at 1 Hz.

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

GL16 and both GC16 modes always wait for update completion, even when selected
as the fast type. This prevents long waveform requests from being queued faster
than the display controller can finish them.
