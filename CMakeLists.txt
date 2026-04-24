cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(PLUGIN_LICENSE "GPL-2.0-or-later")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(WIN32)
  # MSVC cannot link MinGW-built libraries; make libshout optional on Windows
  # until MSVC-compatible binaries are available.
  find_package(LibShout QUIET)
  if(LibShout_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibShout::LibShout)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBSHOUT)
  else()
    message(WARNING "LibShout not found — Windows streaming support requires libshout at build time")
  endif()
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

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
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
    src/radio-output.c
    src/radio-output.h
    src/radio-encoder.h
    src/radio-opus-encoder.c
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
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
