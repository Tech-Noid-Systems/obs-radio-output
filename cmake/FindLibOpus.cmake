# gersemi: off
find_path(
  LibOpus_INCLUDE_DIR
  NAMES opus/opus.h
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    ${LibOpus_ROOT}/include
    $ENV{LIBOPUS_ROOT}/include
    $ENV{LIBOPUS_INCLUDE}
)

if(APPLE)
  # macOS CI provides a Universal static archive built from source; prefer .a
  # so find_library does not search for a .dylib that is absent.
  list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

find_library(
  LibOpus_LIBRARY
  NAMES opus libopus
  PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${LibOpus_ROOT}/lib
    $ENV{LIBOPUS_ROOT}/lib
    $ENV{LIBOPUS_LIB}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibOpus REQUIRED_VARS LibOpus_LIBRARY LibOpus_INCLUDE_DIR)

if(LibOpus_FOUND AND NOT TARGET LibOpus::LibOpus)
  add_library(LibOpus::LibOpus UNKNOWN IMPORTED)
  set_target_properties(
    LibOpus::LibOpus
    PROPERTIES
      IMPORTED_LOCATION "${LibOpus_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibOpus_INCLUDE_DIR}"
  )
endif()
# gersemi: on
