# Windows libshout (MSVC) build — issue #37

Stock Xiph **libshout 2.4.6** does not ship an MSVC build; its only Windows
recipe is MinGW (MSYS2), and a MinGW-built `libshout` cannot link into the
MSVC-built OBS plugin (CRT/ABI mismatch). This directory builds libshout from
the **stock release tarball** with MSVC, against vcpkg-provided
ogg/vorbis/openssl/pthreads — the same approach Mixxx uses to ship libshout on
Windows.

We track **stock 2.4.6** (version parity with the macOS/Linux builds, which use
the same tarball / `libshout3-dev`) plus a tiny, reviewable Windows portability
patch, rather than vendoring a different fork.

## Contents

| File | Purpose |
|------|---------|
| `build.ps1` | Download (SHA-pinned) → patch → configure → build → install `shout.lib` + `include/shout/shout.h` into a prefix. Verifies the `shout_tls_new` symbol (TLS compiled in). |
| `CMakeLists.txt` | Compiles each libshout `.c` with MSVC; links vcpkg ogg/vorbis/openssl/pthreads + `ws2_32`. |
| `config.h` | Hand-written replacement for the autotools `config.h` (winsock, no-POSIX feature flags). |
| `compat.h` | Minimal shim for icecast-common's `compat.h` (absent from the release tarball): `ssize_t` + `str*casecmp` mappings. |
| `windows-msvc.patch` | Two source fixes against stock 2.4.6: guard `<strings.h>` in `encoding.c`; add a `_WIN32`/winsock branch to `connection.c`'s select-header block. |
| `vcpkg.json` (repo root) | Declares the ogg/vorbis/openssl/pthreads dependencies. |

## Triplet

`x64-windows-static-md` — static dependency libs with the **dynamic** CRT
(`/MD`), matching OBS's runtime so the plugin ships without extra dependency
DLLs (mirrors the macOS "no runtime dylib deps" posture).

## Updating the libshout version

1. Bump `$Version` + `$Sha256` in `build.ps1`.
2. Re-extract the new tarball and re-roll `windows-msvc.patch` if the two patched
   files changed (`diff -ruN pristine patched`).
