# Ko Boy License and Third-Party Notices

This document applies to binary and source distributions of Ko Boy.

## Ko Boy

Ko Boy's original frontend, display, input, lifecycle, and supervisor code is:

Copyright (C) 2026 NickelSperiments contributors

Ko Boy is free software: you may redistribute it and/or modify it under the terms of the GNU General Public License, version 2 only.

Ko Boy is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for details.

A release containing this document must also contain the complete text of the GNU General Public License version 2 as [GPL-2.0.txt](GPL-2.0.txt). The exact corresponding source for each release, including build scripts and pinned submodule revisions, is available from the release tag in the [NickelSperiments source repository](https://github.com/ryanmsartor/NickelSperiments). The submodule revisions recorded by that tag identify the exact armGBA, armNES, and NickelHook sources used for its binaries.

## Included third-party software

Ko Boy is linked with or built from the following third-party components. Copyright remains with their respective authors.

| Component | License copy | Source in this repository |
|---|---|---|
| Ko Boy original code | [GPLv2](GPL-2.0.txt) | `src/koboy`, `mk/koboy.mk`, and `mk/koboy-lifecycle.mk` |
| armGBA combined distribution | [GPLv2](GPL-2.0.txt) | `deps/armgba` |
| armGBA native core and libretro glue | [MPL 2.0](MPL-2.0.txt) | `deps/armgba/src` and `deps/armgba/libretro` |
| Gambatte-derived GB/GBC implementation included by armGBA | [GPLv2](GPL-2.0.txt) | `deps/armgba/gambatte` |
| Mega Duck support included by armGBA | [Expat](EXPAT.txt) | `deps/armgba/libretro/megaduck` |
| libretro API and libretro-common support code | [MIT](MIT.txt) | `deps/armgba/libretro/deps/libretro-common` |
| LZMA SDK / 7-Zip decoder source | [Public-domain notices](PUBLIC-DOMAIN-NOTICES.txt) | `deps/armgba/libretro/deps/7zip` |
| Solar Designer MD5 implementation | [Public-domain notices](PUBLIC-DOMAIN-NOTICES.txt) | `deps/armgba/libretro/deps/libretro-common/utils/md5.c` |
| armNES combined core | [GPLv2](GPL-2.0.txt) | `deps/armnes` |
| TriCNES-derived PPU and APU code included by armNES | [MIT](MIT.txt) | `deps/armnes/src/ppu.c` and `deps/armnes/src/apu.c` |
| emu2413 VRC7 sound implementation included by armNES | [MIT](MIT.txt) | `deps/armnes/src/sound/emu2413.c` |
| FCEUmm-derived boards and mapping code included by armNES | [GPLv2](GPL-2.0.txt) | `deps/armnes/src/fceu` and related board implementations |
| MAME-derived VS. System and PlayChoice-10 chip-layout data included by armNES | [BSD 3-Clause](BSD-3-Clause.txt) | `deps/armnes/src/arcade-sets.inc` |
| libretro-common and LZMA SDK support code included by armNES | [MIT](MIT.txt) and [public-domain notices](PUBLIC-DOMAIN-NOTICES.txt) | `deps/armnes/libretro/deps` |
| NickelHook launcher/lifecycle integration | [MIT](MIT.txt) | `deps/NickelHook` |
| NickelModManager More-screen integration pattern | [MIT](MIT.txt) | adapted in `src/koboy-lifecycle.cpp` |

The files named above live beside this document in `LICENSES/KOBOY/` and are intended to be copied into the release package with it. In the source tree, their authoritative upstream texts and notices currently include:

- `deps/armgba/LICENSE`
- `deps/armgba/LICENSES/MPL-2.0.txt`
- `deps/armgba/gambatte/COPYING`
- `deps/armgba/libretro/megaduck/LICENSE`
- `deps/armnes/LICENSE`
- `deps/armnes/LICENSES/MIT.txt`
- `deps/armnes/LICENSES/MIT-emu2413.txt`
- the BSD 3-Clause notices on the MAME sources identified by `deps/armnes/src/arcade-sets.inc`
- `deps/NickelHook/LICENSE`
- the MIT notices embedded in libretro-common source and header files
- the public-domain notices embedded in the LZMA SDK / 7-Zip source files

## Games and firmware

Ko Boy does not include commercial game ROMs, copyrighted console firmware, or Kobo firmware. Users are responsible for supplying game images they are legally entitled to use.

## Project and trademark notice

Ko Boy is an independent project. It is not affiliated with, endorsed by, or sponsored by Rakuten Kobo Inc., Nintendo Co., Ltd., or the authors of the third-party components listed above. Product names and trademarks belong to their respective owners.
