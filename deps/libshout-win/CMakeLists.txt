# Build stock Xiph libshout 2.4.6 as an MSVC static library on Windows (issue #37).
#
# Stock libshout ships only autotools/MinGW build files; MinGW-built libshout
# cannot link into the MSVC-built OBS plugin (CRT/ABI mismatch).  This project
# compiles each libshout .c directly with MSVC against vcpkg-provided
# ogg/vorbis/openssl/pthreads, mirroring the Mixxx recipe.  We track stock 2.4.6
# (version parity with the macOS/Linux builds) plus a tiny Windows portability
# patch — see windows-msvc.patch, compat.h, config.h, and README.md.
#
# Driven by the win-libshout CI step:
#   cmake -S deps/libshout-win -B build-libshout -A x64 \
#     -DLIBSHOUT_SRC=<extracted+patched libshout-2.4.6> \
#     -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
#     -DVCPKG_TARGET_TRIPLET=x64-windows-static-md \
#     -DCMAKE_INSTALL_PREFIX=<prefix>
#   cmake --build build-libshout --config Release
#   cmake --install build-libshout --config Release
cmake_minimum_required(VERSION 3.21)
project(libshout_msvc C)

if(NOT DEFINED LIBSHOUT_SRC)
  message(FATAL_ERROR "Set -DLIBSHOUT_SRC=<path to extracted, patched libshout-2.4.6>")
endif()

find_package(OpenSSL REQUIRED)
find_package(Ogg CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED)

# vcpkg's PThreads4W export name varies by version; find the import lib directly.
find_library(PTHREADS_LIB NAMES pthreadVC3 pthreadVC2 pthread REQUIRED)
message(STATUS "libshout-win: pthreads -> ${PTHREADS_LIB}")

# Generate the public <shout/shout.h> from its template (substitutes TLS flags).
set(SHOUT_THREADSAFE 1)
set(SHOUT_TLS 1)
configure_file("${LIBSHOUT_SRC}/include/shout/shout.h.in" "${CMAKE_BINARY_DIR}/include/shout/shout.h" @ONLY)

set(S "${LIBSHOUT_SRC}/src")
add_library(
  shout
  STATIC
  # core
  "${S}/shout.c"
  "${S}/util.c"
  "${S}/queue.c"
  "${S}/connection.c"
  "${S}/icy.c"
  "${S}/legacy.c"
  "${S}/tls.c"
  # protocols
  "${S}/proto_http.c"
  "${S}/proto_icy.c"
  "${S}/proto_roaraudio.c"
  "${S}/proto_xaudiocast.c"
  # formats
  "${S}/format_mp3.c"
  "${S}/format_ogg.c"
  "${S}/format_text.c"
  "${S}/format_webm.c"
  # codecs (speex/theora excluded — gated off via config.h)
  "${S}/codec_vorbis.c"
  "${S}/codec_opus.c"
  # common
  "${S}/common/avl/avl.c"
  "${S}/common/httpp/httpp.c"
  "${S}/common/httpp/encoding.c"
  "${S}/common/net/sock.c"
  "${S}/common/net/resolver.c"
  "${S}/common/thread/thread.c"
  "${S}/common/timing/timing.c"
)

target_compile_definitions(
  shout
  PRIVATE
    HAVE_CONFIG_H
    WIN32
    _WIN32
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    # PThreads4W: C cleanup scheme, don't redefine struct timespec, static link.
    __CLEANUP_C
    _TIMESPEC_DEFINED
    PTW32_STATIC_LIB
    __PTW32_STATIC_LIB
)

target_include_directories(
  shout
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" # our config.h + compat.h
    "${CMAKE_BINARY_DIR}/include" # generated shout/shout.h
    "${LIBSHOUT_SRC}/include"
    "${S}"
    "${S}/common"
)

target_link_libraries(
  shout
  PRIVATE OpenSSL::SSL OpenSSL::Crypto Ogg::ogg Vorbis::vorbis "${PTHREADS_LIB}" ws2_32
)

# Install layout consumed by the plugin's FindLibShout.cmake: <prefix>/lib/shout.lib
# + <prefix>/include/shout/shout.h.  os.h must ship alongside — the public
# shout.h does `#include <os.h>`, so consumers (the plugin) need it on the
# include path too, not just the libshout build.
install(TARGETS shout ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_BINARY_DIR}/include/shout/shout.h" DESTINATION include/shout)
install(FILES "${LIBSHOUT_SRC}/include/os.h" DESTINATION include)
