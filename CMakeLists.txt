cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(PLUGIN_LICENSE "GPL-2.0-or-later")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_TESTING "Build Catch2 unit tests" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# Vendored obs-websocket plugin API (single header, runtime proc-handler based —
# no link dependency on obs-websocket; registration is a no-op when it's absent).
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps/obs-websocket-api")

if(WIN32)
  # libshout on Windows is the vendored MSVC static build (deps/libshout-win,
  # issue #37).  Being a static archive it leaves its openssl/ogg/vorbis/pthreads/
  # winsock symbols unresolved, so the plugin links them too — all from vcpkg
  # (x64-windows-static-md), same as the libshout build itself.
  find_package(LibShout REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(Ogg CONFIG REQUIRED)
  find_package(Vorbis CONFIG REQUIRED)
  find_library(PTHREADS_LIBRARY NAMES pthreadVC3 pthreadVC2 pthread REQUIRED)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE LibShout::LibShout OpenSSL::SSL OpenSSL::Crypto Ogg::ogg Vorbis::vorbis ${PTHREADS_LIBRARY} ws2_32 crypt32
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBSHOUT)
else()
  find_package(LibShout REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibShout::LibShout)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBSHOUT)
endif()

if(APPLE)
  # libshout on macOS ships as a static archive built --with-openssl (see
  # .github/scripts/build-macos).  The archive references SSL_*/EVP_* symbols
  # but does not embed their definitions, so the plugin binary has to link
  # libssl + libcrypto itself.  OPENSSL_USE_STATIC_LIBS makes find_package
  # prefer .a over .dylib, which keeps the final .plugin bundle free of any
  # runtime dylib dependency on openssl (verify via `otool -L`).  Ubuntu uses
  # libshout3-dev's bundled TLS and Windows streaming is still optional, so
  # this block is Apple-only.
  set(OPENSSL_USE_STATIC_LIBS ON)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
  # MP3 encoding via libmp3lame — optional on Windows until MSVC-compatible binaries are available.
  find_package(LibLame QUIET)
  if(LibLame_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibLame::LibLame)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LAME)
  else()
    message(WARNING "LibLame not found — MP3 encoding not available on Windows")
  endif()
else()
  find_package(LibLame REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibLame::LibLame)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LAME)
endif()

# Opus encoding via libopus (codec) + libogg (container).  Same Windows
# optional-until-MSVC-binaries treatment as libshout / libmp3lame.  The
# encoder path itself lands in a follow-up PR (§C.2 + §C.3); this
# configure-time wiring is a no-op in source until then.
if(WIN32)
  find_package(LibOgg QUIET)
  find_package(LibOpus QUIET)
  if(LibOgg_FOUND AND LibOpus_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibOgg::LibOgg LibOpus::LibOpus)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_OPUS)
  else()
    message(WARNING "LibOgg / LibOpus not found — Opus encoding not available on Windows")
  endif()
else()
  find_package(LibOgg REQUIRED)
  find_package(LibOpus REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibOgg::LibOgg LibOpus::LibOpus)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_OPUS)
endif()

# Vorbis encoding via libvorbis + libvorbisenc (codec) over libogg (container).
# Same Windows optional-until-MSVC-binaries treatment as libshout / libmp3lame /
# libopus.  The encoder source (src/radio-vorbis-encoder.c) lands in a follow-up
# PR (§H.2 + §H.3); this configure-time wiring is a no-op in the binary until a
# source file references the linked symbols.  LibVorbis::LibVorbis already pulls
# in libvorbisenc, but libogg is listed explicitly to complete the
# vorbisenc -> vorbis -> ogg static-link order.
if(WIN32)
  find_package(LibVorbis QUIET)
  if(LibVorbis_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibVorbis::LibVorbis LibOgg::LibOgg)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_VORBIS)
  else()
    message(WARNING "LibVorbis not found — Vorbis encoding not available on Windows")
  endif()
else()
  find_package(LibVorbis REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibVorbis::LibVorbis LibOgg::LibOgg)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_VORBIS)
endif()

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/obs-ws-vendor.c
    src/obs-ws-vendor.h
    src/radio-output.c
    src/radio-output.h
    src/send-buf.c
    src/send-buf.h
    src/radio-encoder.h
    src/radio-opus-encoder.c
    src/radio-vorbis-encoder.c
    src/ogg-opus-headers.c
    src/ogg-opus-headers.h
    src/reconnect.c
    src/reconnect.h
    src/metadata.c
    src/metadata.h
    src/frontend-wire.cpp
    src/frontend-wire.h
    src/radio-output-config-dialog.cpp
    src/radio-output-config-dialog.hpp
    src/radio-output-dock.cpp
    src/radio-output-dock.hpp
    src/listener-poll.cpp
    src/listener-poll.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Unit tests are opt-in (ENABLE_TESTING, default OFF) so the production plugin
# build is bit-for-bit unchanged.  CI turns it on via the macos-ci /
# ubuntu-ci-x86_64 presets; see tests/CMakeLists.txt.
if(ENABLE_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
