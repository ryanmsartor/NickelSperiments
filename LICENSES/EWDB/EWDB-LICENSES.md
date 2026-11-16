# EWDB for Kobo License and Third-Party Notices

This document applies to binary distributions of the EWDB for Kobo package. Copyright remains with the respective authors.

| Component | License copy | Source used by the build |
|---|---|---|
| EWDB | [Apache License 2.0](Apache-2.0.txt) and [EWDB NOTICE](EWDB-NOTICE.txt) | Pinned EWDB checkout in `build/ewdb-src` |
| `github.com/creack/pty` | [MIT](MIT-creack-pty.txt) | `build/ewdb-src/vendor/github.com/creack/pty` |
| `golang.org/x/sys` | [BSD 3-Clause](BSD-3-Clause-golang-x-sys.txt) | `build/ewdb-src/vendor/golang.org/x/sys` |

These copies come from the exact pinned EWDB source checkout and its vendored dependencies. The pinned revision is recorded in `scripts/build-ewdbd.sh`.

EWDB for Kobo is an independent package. It is not affiliated with or endorsed by Google LLC, the Android Open Source Project, or Rakuten Kobo Inc. Product names and trademarks belong to their respective owners.
