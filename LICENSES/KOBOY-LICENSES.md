# Ko Boy License and Third-Party Notices

This document applies to binary and source distributions of Ko Boy.

## Ko Boy

Ko Boy's original frontend, display, input, lifecycle, and supervisor code is:

Copyright (C) 2026 NickelSperiments contributors

Ko Boy is free software: you may redistribute it and/or modify it under the
terms of the GNU General Public License, version 2 only.

Ko Boy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE. See the GNU General Public License for details.

A release containing this document must also contain the complete text of the
GNU General Public License version 2 as [GPL-2.0.txt](GPL-2.0.txt). The exact
corresponding source for a release, including build scripts and pinned
submodule revisions, must be made available under the GPL.

## Included third-party software

Ko Boy is linked with or built from the following third-party components.
Copyright remains with their respective authors.

| Component | License copy | Source in this repository |
|---|---|---|
| Ko Boy original code | [GPLv2](GPL-2.0.txt) | `src/koboy`, `mk/koboy.mk`, and `mk/koboy-lifecycle.mk` |
| armGBA | [GPLv2](GPL-2.0.txt) | `deps/armgba` |
| Gambatte-derived GB/GBC implementation included by armGBA | [GPLv2](GPL-2.0.txt) | `deps/armgba/gambatte` |
| Mega Duck support included by armGBA | [Expat](EXPAT.txt) | `deps/armgba/libretro/megaduck` |
| libretro API and libretro-common support code | [MIT](MIT.txt) | `deps/armgba/libretro/deps/libretro-common` |
| LZMA SDK / 7-Zip decoder source | [Public-domain notices](PUBLIC-DOMAIN-NOTICES.txt) | `deps/armgba/libretro/deps/7zip` |
| Solar Designer MD5 implementation | [Public-domain notices](PUBLIC-DOMAIN-NOTICES.txt) | `deps/armgba/libretro/deps/libretro-common/utils/md5.c` |
| NickelHook lifecycle integration | [MIT](MIT.txt) | `deps/NickelHook` |

The files named above live beside this document in `LICENSES/` and are intended
to be copied into the release package with it. In the source tree, their
authoritative upstream texts and notices currently include:

- `deps/armgba/LICENSE`
- `deps/armgba/gambatte/COPYING`
- `deps/armgba/libretro/megaduck/LICENSE`
- `deps/NickelHook/LICENSE`
- the MIT notices embedded in libretro-common source and header files
- the public-domain notices embedded in the LZMA SDK / 7-Zip source files

## Games and firmware

Ko Boy does not include commercial game ROMs, copyrighted console firmware, or
Kobo firmware. Users are responsible for supplying game images they are legally
entitled to use.

## Project and trademark notice

Ko Boy is an independent project. It is not affiliated with, endorsed by, or
sponsored by Rakuten Kobo Inc., Nintendo Co., Ltd., or the authors of the
third-party components listed above. Product names and trademarks belong to
their respective owners.
