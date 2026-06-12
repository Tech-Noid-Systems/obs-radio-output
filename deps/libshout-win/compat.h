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

#ifdef _MSC_VER
/* ssize_t: match libshout's own include/os.h (`typedef int ssize_t`), which the
 * public <shout/shout.h> already pulls into every TU.  Using the SAME type makes
 * this a legal identical typedef redefinition (C11 §6.7) regardless of include
 * order — defining it as a different width (e.g. SSIZE_T) trips C2371.
 * (POSIX function name mappings — strcasecmp etc. — live in config.h so they
 * reach every TU, not just the ones that include this header.) */
typedef int ssize_t;
#endif /* _MSC_VER */

#endif /* _WIN32 */

#endif /* LIBSHOUT_WIN_COMPAT_H */
