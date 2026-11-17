Ko Boy
======

Ko Boy is an e-ink-focused Game Boy, Game Boy Color, Game Boy Advance, and
Mega Duck player built around armGBA. This release targets the Kobo Clara BW;
other models are not yet supported.

Place legally obtained ROM images in the matching directory below:

  /mnt/onboard/KoBoy/Roms/Game Boy/
  /mnt/onboard/KoBoy/Roms/Game Boy Color/
  /mnt/onboard/KoBoy/Roms/Game Boy Advance/
  /mnt/onboard/KoBoy/Roms/Mega Duck/

Ko Boy appears in Nickel's More screen after installation and reboot.

The on-device browser supports uncompressed ROMs and ZIP archives. Saves,
automatic resume states, and manual states are stored under
/mnt/onboard/KoBoy. Optional recognized BIOS files can be placed directly in
/mnt/onboard/KoBoy/BIOS; otherwise Ko Boy uses armGBA's built-in open-source
boot ROMs. Pair Bluetooth headphones in Nickel before launching a game if
audio is desired.

Touch controls support simultaneous direction and face-button presses,
single-touch diagonals, and rolling between A and B while retaining the first
button. The in-game menu provides save/load state, volume, display settings,
reset, game switching, and return to Nickel.

To uninstall Ko Boy while preserving all ROMs, saves, states, and settings,
create an empty file at:

  /mnt/onboard/.adds/koboy/uninstall

Then reboot the Kobo. The program files and Nickel integration are removed
during startup. Everything under /mnt/onboard/KoBoy remains in place.

License texts and third-party notices are in the adjacent LICENSES directory.
