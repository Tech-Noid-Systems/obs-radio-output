/*
 * compat.h — minimal Windows/MSVC compatibility shim for building libshout
 * 2.4.6 from the stock Xiph release tarball.
 *
 * libshout's src/common/net/sock.h does `#elif _WIN32 #include <compat.h>`, and
 * src/common/httpp/encoding.c (patched here) pulls it in instead of the POSIX
 * <strings.h>.  The real compat.h lives in icecast-common and is NOT shipped in
 * the libshout release tarball — this file supplies only the handful of symbols
 * libshout's sources actually reference on Windows.  See windows-msvc.patch and
 * README.md in this directory.
 */
#ifndef LIBSHOUT_WIN_COMPAT_H
#define LIBSHOUT_WIN_COMPAT_H

#ifdef _WIN32
/* winsock2.h before ws2tcpip.h (which provides socklen_t, struct in6_addr,
 * getaddrinfo — used by sock.c/resolver.c but NOT pulled in by winsock2.h
 * alone). Include guards make this safe even where sock.h already included
 * winsock2.h first. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <BaseTsd.h> /* SSIZE_T */

/* MSVC has no ssize_t; SSIZE_T is the Win32 SDK's signed size type (64-bit on
 * x64, matching POSIX ssize_t semantics).  Coordinate with PThreads4W / CRT
 * headers that may also define it: check (and set) every common guard so
 * whichever header is included first wins and the other skips. */
#if !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T_) && !defined(__ssize_t_defined)
#define _SSIZE_T_DEFINED
#define _SSIZE_T_
typedef SSIZE_T ssize_t;
#endif

#ifdef _MSC_VER
/* POSIX names libshout calls (encoding.c, util.c) → MSVC equivalents. */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#ifndef strdup
#define strdup _strdup
#endif
#endif /* _MSC_VER */

#endif /* _WIN32 */

#endif /* LIBSHOUT_WIN_COMPAT_H */
