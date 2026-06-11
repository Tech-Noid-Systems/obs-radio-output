/*
 * config.h — hand-written for building stock libshout 2.4.6 with MSVC on
 * Windows.  Replaces the autotools-generated config.h with fixed values
 * known-correct for the Windows/MSVC target.
 *
 * Values derived from libshout-2.4.6/config.h.in plus the source's own _WIN32
 * guards (sock.c uses WSAStartup, timing.c uses Sleep/ftime, thread.c uses
 * PThreads4W).  check_symbol_exists-style feature detection is deliberately
 * avoided: it misfires on winsock functions (declared in winsock2.h, which CMake
 * does not pull in for the probe), so fixed values are more reliable here.
 *
 * Notable OMISSIONS (POSIX-only — left undefined so the sources take their
 * _WIN32 branches): HAVE_UNISTD_H (→ sock.h includes <compat.h>),
 * HAVE_SYS_SELECT_H (→ connection.c includes <winsock2.h>, see windows-msvc.patch),
 * HAVE_STRINGS_H, HAVE_SYS_SOCKET_H, HAVE_ARPA_INET_H, HAVE_GETTIMEOFDAY,
 * HAVE_NANOSLEEP, HAVE_WRITEV.
 */
#ifndef LIBSHOUT_MSVC_CONFIG_H
#define LIBSHOUT_MSVC_CONFIG_H

/* Version */
#define VERSION "2.4.6"
#define PACKAGE_VERSION "2.4.6"

/* Codecs compiled in (speex/theora deliberately excluded — see CMakeLists.txt) */
#define HAVE_OGG 1
#define HAVE_VORBIS 1

/* TLS */
#define HAVE_OPENSSL 1

/* Threads (PThreads4W from vcpkg).  The PTHREAD_* enum constants come from
 * <pthread.h> itself once __CLEANUP_C is defined (see CMakeLists.txt). */
#define HAVE_PTHREAD 1
#define PTHREAD_CREATE_JOINABLE 1

/* C runtime / headers present on MSVC */
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_C99_INTTYPES 1
#define HAVE_STDARG_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1

/* timing.c: no gettimeofday on MSVC → it falls back to _ftime (sys/timeb.h). */
#define HAVE_SYS_TIMEB_H 1
#define HAVE_FTIME 1

/* Networking — winsock2, not BSD sockets */
#define HAVE_WINSOCK2_H 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETNAMEINFO 1
#define HAVE_INET_PTON 1
#define HAVE_SOCKLEN_T 1
#define HAVE_STRUCT_SOCKADDR_STORAGE_SS_FAMILY 1

/* Sizes (MSVC x64 is LLP64: long is 32-bit) */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8

#endif /* LIBSHOUT_MSVC_CONFIG_H */
