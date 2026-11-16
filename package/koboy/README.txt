Ko Boy
======

Ko Boy is an experimental emulator frontend for Kobo e-readers.

Place legally obtained ROM images in the matching directory below:

  /mnt/onboard/KoBoy/Roms/Game Boy/
  /mnt/onboard/KoBoy/Roms/Game Boy Color/
  /mnt/onboard/KoBoy/Roms/Game Boy Advance/
  /mnt/onboard/KoBoy/Roms/Mega Duck/

Ko Boy appears in Nickel's More screen after installation and reboot.

To uninstall Ko Boy while preserving all ROMs, saves, states, and settings,
create an empty file at:

  /mnt/onboard/.adds/koboy/uninstall

Then reboot the Kobo. The program files and Nickel integration are removed
during startup. Everything under /mnt/onboard/KoBoy remains in place.

License texts and third-party notices are in the adjacent LICENSES directory.
