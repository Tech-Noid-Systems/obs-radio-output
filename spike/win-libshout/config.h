/*
 * Hand-written config.h for building stock libshout 2.4.6 with MSVC on Windows.
 *
 * This is the SPIKE for issue #37: it replaces the autotools-generated config.h
 * with fixed values known-correct for the Windows/MSVC target. (The production
 * build should generate this via CMake feature-detection for portability; for a
 * Windows-only buildability spike, fixed values are more reliable than
 * check_symbol_exists, which misfires on winsock functions.)
 *
 * Values derived from libshout-2.4.6/config.h.in + the source's own _WIN32
 * guards (sock.c uses WSAStartup, timing.c uses Sleep/ftime, thread.c uses
 * PThreads4W).
 */
#ifndef LIBSHOUT_MSVC_CONFIG_H
#define LIBSHOUT_MSVC_CONFIG_H

/* Version */
#define VERSION         "2.4.6"
#define PACKAGE_VERSION "2.4.6"

/* Codecs we compile in (speex/theora deliberately excluded) */
#define HAVE_OGG     1
#define HAVE_VORBIS  1
/* opus container parsing needs only <ogg/ogg.h>; no HAVE_OPUS macro exists */

/* TLS */
#define HAVE_OPENSSL 1

/* Threads (PThreads4W from vcpkg) */
#define HAVE_PTHREAD            1
#define PTHREAD_CREATE_JOINABLE 1
/* #undef HAVE_PTHREAD_SPIN_LOCK */

/* C runtime / headers present on MSVC */
#define HAVE_STDINT_H    1
#define HAVE_INTTYPES_H  1
#define HAVE_C99_INTTYPES 1
#define HAVE_STDARG_H    1
#define HAVE_STDLIB_H    1
#define HAVE_STRING_H    1
#define HAVE_SYS_STAT_H  1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIMEB_H 1
#define HAVE_FTIME       1

/* Networking — winsock2, not BSD sockets */
#define HAVE_WINSOCK2_H 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETNAMEINFO 1
#define HAVE_INET_PTON   1
#define HAVE_SOCKLEN_T   1
#define HAVE_STRUCT_SOCKADDR_STORAGE_SS_FAMILY 1

/* Sizes (MSVC x64 is LLP64: long is 32-bit) */
#define SIZEOF_SHORT     2
#define SIZEOF_INT       4
#define SIZEOF_LONG      4
#define SIZEOF_LONG_LONG 8

/*
 * Deliberately NOT defined on Windows/MSVC (POSIX-only):
 *   HAVE_ARPA_INET_H HAVE_SYS_SOCKET_H HAVE_SYS_SELECT_H HAVE_SYS_UIO_H
 *   HAVE_UNISTD_H HAVE_STRINGS_H HAVE_DLFCN_H HAVE_MEMORY_H
 *   HAVE_WRITEV HAVE_GETTIMEOFDAY HAVE_NANOSLEEP HAVE_INET_ATON
 *   HAVE_STRCASESTR (libshout provides a fallback) HAVE_SETHOSTENT
 *   HAVE_ENDHOSTENT TIME_WITH_SYS_TIME HAVE_SPEEX HAVE_THEORA
 */

#endif /* LIBSHOUT_MSVC_CONFIG_H */
