# Spike (issue #37): can stock libshout 2.4.6 build as an MSVC static lib?
#
# This does NOT touch the plugin. It downloads-and-builds just libshout to prove
# the Mixxx-style "vendor the source + compile each .c with MSVC, link vcpkg
# deps" recipe works on our Windows runner. If `shout.lib` comes out, the #37
# estimate (~6-10 eng-days) holds and the real PR vendors this for keeps.
#
# Configure (driven by the spike-win-libshout.yaml workflow):
#   cmake -S spike/win-libshout -B build-spike -A x64 \
#     -DLIBSHOUT_SRC=<extracted libshout-2.4.6> \
#     -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake_minimum_required(VERSION 3.21)
project(libshout_msvc_spike C)

if(NOT DEFINED LIBSHOUT_SRC)
  message(FATAL_ERROR "Set -DLIBSHOUT_SRC=<path to extracted libshout-2.4.6>")
endif()

find_package(OpenSSL REQUIRED)
find_package(Ogg CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED)

# vcpkg 'pthreads' (PThreads4W) export name varies; find the import lib directly.
find_library(PTHREADS_LIB NAMES pthreadVC3 pthreadVC2 pthread REQUIRED)
message(STATUS "spike: pthreads -> ${PTHREADS_LIB}")

# Generate the public <shout/shout.h> from its template (substitutes TLS flags).
set(SHOUT_THREADSAFE 1)
set(SHOUT_TLS 1)
configure_file(
  "${LIBSHOUT_SRC}/include/shout/shout.h.in"
  "${CMAKE_BINARY_DIR}/include/shout/shout.h" @ONLY)

set(S "${LIBSHOUT_SRC}/src")
add_library(shout STATIC
  # core
  "${S}/shout.c" "${S}/util.c" "${S}/queue.c" "${S}/connection.c"
  "${S}/icy.c" "${S}/legacy.c" "${S}/tls.c"
  # protocols
  "${S}/proto_http.c" "${S}/proto_icy.c" "${S}/proto_roaraudio.c" "${S}/proto_xaudiocast.c"
  # formats
  "${S}/format_mp3.c" "${S}/format_ogg.c" "${S}/format_text.c" "${S}/format_webm.c"
  # codecs (speex/theora excluded — gated off via config.h)
  "${S}/codec_vorbis.c" "${S}/codec_opus.c"
  # common
  "${S}/common/avl/avl.c"
  "${S}/common/httpp/httpp.c" "${S}/common/httpp/encoding.c"
  "${S}/common/net/sock.c" "${S}/common/net/resolver.c"
  "${S}/common/thread/thread.c" "${S}/common/timing/timing.c")

target_compile_definitions(shout PRIVATE
  HAVE_CONFIG_H WIN32 _WIN32
  _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
  __CLEANUP_C _TIMESPEC_DEFINED)   # PThreads4W: C cleanup + don't redefine timespec

target_include_directories(shout PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"        # our hand-written config.h
  "${CMAKE_BINARY_DIR}/include"        # generated shout/shout.h
  "${LIBSHOUT_SRC}/include"
  "${S}" "${S}/common")

target_link_libraries(shout PRIVATE
  OpenSSL::SSL OpenSSL::Crypto
  Ogg::ogg Vorbis::vorbis
  "${PTHREADS_LIB}" ws2_32)
