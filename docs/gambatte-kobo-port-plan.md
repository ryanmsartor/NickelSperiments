# Gambatte Kobo port plan

## Summary

Gambatte is a strong emulator core for a lightweight Kobo Game Boy emulator,
but the Kobo display will define what is playable. The CPU should handle Game
Boy emulation comfortably; e-ink cannot approach the Game Boy's native refresh
rate. The initial target should be turn-based RPGs, puzzle games, and slower
adventures rather than platformers or rhythm games.

Gambatte's core is portable C++, exposes a straightforward 160-by-144-pixel
framebuffer and input callback, and already has ARMv7 build support. This makes
it a better foundation than porting a complete RetroArch stack.

References:

- [Gambatte repository](https://github.com/libretro/gambatte-libretro/)
- [Gambatte core API](https://raw.githubusercontent.com/libretro/gambatte-libretro/master/libgambatte/include/gambatte.h)

## Proposed architecture

Build a small Kobo-specific frontend around `libgambatte`:

```text
ROM + save data
      |
      v
 libgambatte ------> audio buffer ------> Bluetooth audio, later
      |
      v
160x144 framebuffer
      |
grayscale conversion + integer scaling
      |
latest-frame presentation at approximately 6-10 Hz
      |
Kobo framebuffer + e-ink refresh ioctl
```

The emulator should continue internally at normal Game Boy speed while the
display frontend presents only the newest completed frame. This preserves game
timing, input, saves, and eventually audio without trying to submit 60 e-ink
updates per second.

The port should use `libgambatte` directly instead of bringing in RetroArch or
the existing SDL and Qt frontends. The Kobo frontend only needs to provide:

- ROM loading and persistent save paths.
- Game Boy input state.
- Video conversion and presentation.
- Application lifecycle and cleanup.
- Optional audio output in a later phase.

## Display strategy

For the Kobo Clara BW's 1072-by-1448 display:

- Render the game at 4x or 5x integer scale.
- Put the game viewport in the upper portion of the display.
- Reserve the lower portion for large touch controls.
- Start with monochrome A2 updates for maximum responsiveness.
- Experiment with four-level grayscale modes for slower games.
- Perform an occasional full-quality GC16 cleanup on pause, menus, scene
  transitions, or after a configurable number of frames.

FBInk documents approximately 120 ms for A2 and 450 ms for GC16 as broad
expectations, so even the fast path is fundamentally single-digit FPS
territory. See the [FBInk waveform
documentation](https://github.com/NiLuJe/FBInk/blob/master/fbink.h).

The first prototype should test:

- A2 at 6, 8, and 10 requested updates per second.
- Full-frame versus game-viewport-only refreshes.
- One-bit thresholding versus ordered dithering.
- Ghosting after 30, 60, and 120 seconds.
- Whether four-level modes are fast enough to be useful.

The existing NickelRefresh work provides a starting point for this
characterization. Its results should determine the initial presentation rate
and waveform policy rather than assuming that every requested update becomes a
visible frame.

The emulation and presentation clocks must remain independent. Input and
emulation should continue at their normal cadence when the display skips
frames. The presenter should always select the newest complete frame rather
than queueing stale frames and accumulating visible latency.

## Controls

On the Clara BW, the initial controller should be a multitouch overlay:

- D-pad on the lower left.
- A and B buttons on the lower right.
- Start and Select in the middle.
- A tap or long-press gesture to open the emulator menu.
- The power button reserved for suspend or emergency exit.

Input should be read directly from Linux evdev and processed independently of
screen presentation. Kobo models have different touch protocols and rotation
quirks, so device detection and coordinate normalization need their own
abstraction. KOReader's porting documentation confirms that Kobo software
commonly reads framebuffer and evdev interfaces directly. See the [KOReader
porting guide](https://github.com/koreader/koreader/blob/master/doc/Porting.md).

The first input test should establish whether the Clara BW reports two
simultaneous contacts reliably. Gameplay requires holding a direction while
pressing A or B. If multitouch is unreliable, possible fallbacks include
sticky directional input, gesture controls, or a later physical-controller
backend.

Bluetooth controllers can be investigated later, but they should not be an
MVP dependency.

## Audio

Audio should be deferred until video and lifecycle behavior are stable.

The Clara BW supports Bluetooth audio devices, so sound may ultimately be
possible through the firmware's audio stack. Access from third-party software
still needs real-device investigation. See [Kobo's Bluetooth audio
documentation](https://help.kobo.com/hc/en-us/articles/4406292712471-Listen-to-audiobooks-on-your-Kobo-eReader).

The MVP should run muted. A later phase can identify the active audio
interfaces and test PCM playback without interfering with Nickel. Audio must
follow emulation time, not the much slower display presentation rate.

## Licensing constraint

Licensing needs resolution before distributing a combined binary:

- Gambatte identifies itself as GPL version 2.
- FBInk is GPL version 3.

The Gambatte headers appear to specify GPL-2.0-only rather than "version 2 or
later," which generally makes direct linking with GPLv3-only FBInk
problematic.

References:

- [Gambatte license header](https://raw.githubusercontent.com/libretro/gambatte-libretro/master/libgambatte/include/gambatte.h)
- [FBInk license](https://raw.githubusercontent.com/NiLuJe/FBInk/master/LICENSE)

The safest initial architecture is therefore:

- Build the frontend and Gambatte together under GPL-2.0.
- Implement the minimal framebuffer mapping and refresh ioctl backend without
  copying FBInk implementation code.
- Use Linux evdev directly.
- Keep FBInk as a development reference, not a linked dependency.

A formal license review is still appropriate before publishing binaries.

## Implementation phases

### 1. Feasibility probe

Implementation has begun with the `gambatte-bench` Make target and
`build/kobo-gambatte-bench` headless runner. The runner measures emulation
throughput and memory use and validates an in-memory save-state round trip.

The first real-device ARMv7 test passed on a Kobo Clara BW on 2026-11-14 using
the user-supplied US ROM for *Rolan's Curse II*. The statically linked ARM EABI5
benchmark and ROM matched their workstation SHA-256 hashes on the reader. All
three 3,600-frame trials passed save-state replay and the cartridge's 8 KiB
save-RAM round trip:

| Trial | Emulated FPS | Real-time multiple | Elapsed time |
| ---: | ---: | ---: | ---: |
| 1 | 1,615.80 | 27.05x | 2.228 s |
| 2 | 1,586.58 | 26.56x | 2.269 s |
| 3 | 1,612.69 | 27.00x | 2.232 s |

The framebuffer checksum was identical in all three trials. Reported maximum
resident memory remained at or below 8,208 KiB. This comfortably passes the
full-speed and CPU-headroom feasibility criterion for the tested DMG ROM. The
core should still be exercised with representative GBC and other mapper types,
but CPU performance is no longer the primary Step 1 risk.

- Pin a Gambatte revision.
- Cross-compile `libgambatte` for Kobo ARMv7.
- Build a headless runner that loads a freely distributable test ROM.
- Run several emulated minutes and measure CPU and memory use.
- Verify save RAM and save-state round trips.
- Confirm that the binary runs against the Kobo firmware's userspace ABI.

Go/no-go criterion: stable full-speed emulation with substantial CPU headroom.

### 2. Framebuffer prototype

Implementation has begun with `make gambatte-display-test`. The current
prototype is intentionally synthetic: it writes a scaled, animated 160-by-144
monochrome test pattern into a saved viewport of the Clara BW's 32-bit HWTCON
framebuffer. It submits partial A2 updates at a configurable rate, periodically
performs a full GC16 cleanup, reports submission timing and dropped frames, and
restores the saved viewport with GC16 on normal or handled-signal exit.

This is a developer diagnostic, not an installable package. Run it only from a
recovery-capable ADB session. It currently targets the MediaTek `hwtcon`
framebuffer ABI and rejects other framebuffer drivers instead of guessing.
Nickel is not stopped yet, so the first device test should be brief and should
verify restoration before longer ghosting and latency measurements.

The first Clara BW device tests passed on 2026-11-14. A three-second synthetic
run submitted all 24 requested frames at 8 Hz with no drops. Average update
submission time was 5.41 ms; the 78.42 ms maximum included a waited GC16
cleanup. A separate SIGTERM test interrupted an active run after 20 frames and
returned status 130 after restoring the viewport. Neither test logged an
HWTCON/display-driver error.

`make gambatte-display` now builds the next integration slice. It runs Gambatte
at the Game Boy's native frame rate and presents only the latest completed
frame through the tested backend. With the user-supplied US ROM for *Rolan's
Curse II*, a five-second Clara BW run emulated 298 frames, presented 40 frames
at 8 Hz, performed two GC16 cleanups, restored the viewport, and passed without
display errors. The binary and ROM matched their workstation SHA-256 hashes on
the reader before execution.

The runner now uses 5x integer scaling (up from the initial 4x test), draws a
conventional D-pad at the lower left, A and B at the lower right, and labeled
Start and Select controls along the bottom. It reads the Clara BW's
`cyttsp5_mt` Type-B multitouch stream directly, handles simultaneous contacts,
applies the panel's swapped-axis and mirrored-X portrait transform, and uses
`EVIOCGRAB` so touches cannot reach Nickel.

Before changing the display it sends SIGSTOP to Nickel, saves the complete
framebuffer, clears it, and performs a full GC16 refresh. Cleanup restores the
complete saved framebuffer, releases the touchscreen grab, and sends SIGCONT
to Nickel, in that order. A real-device lifecycle test observed Nickel change
from sleeping to stopped while the emulator ran and return to sleeping after
exit. A final five-second regression at 5x scaling emulated 298 frames,
presented 40, completed two GC16 cleanups, restored the screen, and resumed
Nickel without a display-driver error.

Touch input is sampled once per emulated Game Boy frame, approximately 59.73
Hz, independently of the 8 Hz display cadence. Distinct evdev button states are
queued in order, so a complete tap between samples is delivered as one pressed
frame followed by a released frame. Tracking-ID releases are supported, and a
global zero-valued `BTN_TOUCH` or `BTN_TOOL_FINGER` report authoritatively
clears every slot to prevent a stale contact from holding a direction.

A focused 20-second device test captured 48 input transitions. Input was active
for 258 of 1,194 emulated frames rather than remaining stuck after the first
touch; the run completed 160 presentations and restored Nickel successfully.
The A and B controls use literal bitmap `A` and `B` glyphs rather than
approximate hand-drawn marks.

This runner still has no audio or save-file persistence. In-process cleanup
covers normal exit, exceptions, SIGINT, SIGTERM, and SIGHUP; a future external
supervisor is still required to recover automatically from SIGKILL or a hard
crash. It remains a developer diagnostic rather than an installable emulator.

- Take exclusive ownership of the framebuffer.
- Render a synthetic 160-by-144 animated test pattern.
- Add integer scaling and grayscale conversion.
- Benchmark A2 and four-level presentation modes.
- Measure request timing, visible update timing, ghosting, and CPU use.
- Restore the previous screen, bit depth, rotation, and framebuffer state on
  exit.

Go/no-go criterion: a stable 6-10 Hz presentation path without display-driver
errors or an unacceptable buildup of latency.

### 3. Playable vertical slice

- Connect Gambatte video to the display backend.
- Add touch D-pad, A, B, Start, and Select controls.
- Use a freely distributable test ROM; do not bundle commercial ROMs.
- Add pause, exit, reset, and save-state controls.
- Test turn-based RPG and puzzle-game workloads first.
- Confirm that save RAM is flushed safely on pause and exit.

### 4. Lifecycle and safety

- Stop or suspend Nickel while the emulator owns the framebuffer and input.
- Restore Nickel cleanly after normal exit, signals, crashes, suspend, and low
  battery events.
- Install signal handlers and a cleanup wrapper that restores display state
  even when the emulator terminates abnormally.
- Keep ROMs and saves under `/mnt/onboard/.adds/` in a project-specific
  directory.
- Never alter, bundle, or redistribute user ROMs.
- Ensure firmware upgrades and package removal leave Nickel bootable.

### 5. Packaging

- Produce a project-specific KoboRoot package with an unambiguous release
  filename.
- Install the executable and cleanup scripts under `/usr/local`.
- Keep user content, configuration, and saves on onboard storage.
- Add a NickelMenu launcher when NickelMenu is present while retaining a shell
  launcher for development and recovery.
- Include licenses, source availability information, and exact pinned upstream
  revisions.
- Provide an uninstall path that removes system files without deleting ROMs or
  saves.

### 6. Optional enhancements

- Bluetooth audio.
- Physical Bluetooth controller support.
- Per-game display and palette profiles.
- Automatic GC16 cleanup scheduling.
- Palette presets optimized for monochrome e-ink.
- A ROM browser with a lightweight text interface.
- Rewind only if memory, CPU, and storage behavior prove acceptable.

## Main risks

- E-ink latency makes action games unpleasant regardless of emulator speed.
- Touch controls may not provide reliable simultaneous directional and button
  input until protocol quirks are handled.
- Nickel and the emulator must not compete for framebuffer or input ownership.
- Display state must be restored reliably after every exit path.
- Audio integration could be firmware-specific.
- The Gambatte and FBInk licensing combination rules out the easiest direct
  reuse path.
- Device-specific framebuffer, rotation, touch, and waveform behavior may make
  support beyond the Clara BW substantially more work.

## Recommendation and first milestone

Proceed with Gambatte, but define the result as an e-ink Game Boy player
optimized for slower games rather than a conventional 60 FPS handheld
emulator.

The first meaningful milestone should be a freely distributable test ROM or a
user-supplied turn-based game running on the Clara BW with:

- Reliable multitouch controls.
- Stable save data.
- Full-speed internal emulation.
- A measured 6-10 Hz latest-frame display path.
- Clean entry from and return to Nickel.
- No permanent framebuffer, input, or power-management side effects.

If that milestone feels usable for a turn-based game, continue with packaging
and broader game testing. If it does not, further emulator optimization will
not solve the primary limitation; effort should instead focus on display
presentation, interaction design, or a more static class of games.
