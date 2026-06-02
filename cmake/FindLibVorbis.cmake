# gersemi: off
find_path(
  LibVorbis_INCLUDE_DIR
  NAMES vorbis/codec.h
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    ${LibVorbis_ROOT}/include
    $ENV{LIBVORBIS_ROOT}/include
    $ENV{LIBVORBIS_INCLUDE}
)

if(APPLE)
  # macOS CI provides Universal static archives built from source; prefer .a
  # so find_library does not search for a .dylib that is absent.
  list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

# libvorbis ships as two libraries: libvorbis (core analysis/DSP) and
# libvorbisenc (encode-only entry points such as vorbis_encode_init).  We use
# symbols from both; libvorbisfile (decode helpers) is not needed.
find_library(
  LibVorbis_LIBRARY
  NAMES vorbis libvorbis
  PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${LibVorbis_ROOT}/lib
    $ENV{LIBVORBIS_ROOT}/lib
    $ENV{LIBVORBIS_LIB}
)

find_library(
  LibVorbis_ENC_LIBRARY
  NAMES vorbisenc libvorbisenc
  PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${LibVorbis_ROOT}/lib
    $ENV{LIBVORBIS_ROOT}/lib
    $ENV{LIBVORBIS_LIB}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  LibVorbis
  REQUIRED_VARS LibVorbis_LIBRARY LibVorbis_ENC_LIBRARY LibVorbis_INCLUDE_DIR
)

if(LibVorbis_FOUND AND NOT TARGET LibVorbis::LibVorbis)
  add_library(LibVorbis::LibVorbis UNKNOWN IMPORTED)
  # Static-link ordering: libvorbisenc references symbols defined in libvorbis
  # core, so on single-pass linkers (GNU ld on Ubuntu) the encode archive must
  # appear *before* the core archive.  Put vorbisenc in IMPORTED_LOCATION (first
  # on the link line) and vorbis core in INTERFACE_LINK_LIBRARIES (after it);
  # libogg is appended by CMakeLists.txt, completing the enc -> core -> ogg
  # order.  macOS ld64 is not order-sensitive, but the explicit ordering keeps
  # the Ubuntu shared-lib and any future static build correct too.
  set_target_properties(
    LibVorbis::LibVorbis
    PROPERTIES
      IMPORTED_LOCATION "${LibVorbis_ENC_LIBRARY}"
      INTERFACE_LINK_LIBRARIES "${LibVorbis_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibVorbis_INCLUDE_DIR}"
  )
endif()
# gersemi: on
