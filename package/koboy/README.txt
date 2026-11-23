Ko Boy
======

Ko Boy is an e-ink-focused Game Boy, Game Boy Color, Game Boy Advance,
Mega Duck, NES, Famicom Disk System, Gamate, and Watara Supervision player.
This release targets the Kobo Clara BW; other models are not yet supported.

Place legally obtained ROM images in the matching directory below:

  /mnt/onboard/KoBoy/Roms/Game Boy/
  /mnt/onboard/KoBoy/Roms/Game Boy Color/
  /mnt/onboard/KoBoy/Roms/Game Boy Advance/
  /mnt/onboard/KoBoy/Roms/Mega Duck/
  /mnt/onboard/KoBoy/Roms/Nintendo Entertainment System/
  /mnt/onboard/KoBoy/Roms/Famicom Disc System/
  /mnt/onboard/KoBoy/Roms/Gamate/
  /mnt/onboard/KoBoy/Roms/Supervision/

Ko Boy appears in Nickel's More screen after installation and reboot.

The on-device browser supports uncompressed ROMs and ZIP archives. Saves,
automatic resume states, and manual states are stored under
/mnt/onboard/KoBoy. Optional recognized BIOS files can be placed directly in
/mnt/onboard/KoBoy/BIOS; otherwise Ko Boy uses armGBA's built-in open-source
boot ROMs. Gamate requires gamate_bios_umc.bin or gamate_bios_bit.bin in that
directory. Gamate cartridge dumps may use .gam or .bin; Ko Boy distinguishes
Gamate and Mega Duck .bin images from their contents. Pair Bluetooth headphones
in Nickel before launching a game if audio is desired.

Touch controls support simultaneous direction and face-button presses,
single-touch diagonals, and rolling between A and B while retaining the first
button. Bluetooth gamepads are detected automatically; the tested controller is
an 8BitDo Micro 2 in Switch mode. The in-game menu provides save/load state,
volume, display and curated game settings, reset, game switching, FDS disk
control when applicable, and return to Nickel.

To uninstall Ko Boy while preserving all ROMs, saves, states, and settings,
create an empty file at:

  /mnt/onboard/.adds/koboy/uninstall

Then reboot the Kobo. The program files and Nickel integration are removed
during startup. Everything under /mnt/onboard/KoBoy remains in place.

License texts and third-party notices are in the adjacent LICENSES directory.
The exact corresponding source for this release, including its build scripts
and pinned dependencies, is available from the matching release tag at:

  https://github.com/ryanmsartor/NickelSperiments
