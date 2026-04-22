# gersemi: off
find_path(
  LibOgg_INCLUDE_DIR
  NAMES ogg/ogg.h
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    ${LibOgg_ROOT}/include
    $ENV{LIBOGG_ROOT}/include
    $ENV{LIBOGG_INCLUDE}
)

if(APPLE)
  # macOS CI provides a Universal static archive built from source; prefer .a
  # so find_library does not search for a .dylib that is absent.
  list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

find_library(
  LibOgg_LIBRARY
  NAMES ogg libogg
  PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${LibOgg_ROOT}/lib
    $ENV{LIBOGG_ROOT}/lib
    $ENV{LIBOGG_LIB}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibOgg REQUIRED_VARS LibOgg_LIBRARY LibOgg_INCLUDE_DIR)

if(LibOgg_FOUND AND NOT TARGET LibOgg::LibOgg)
  add_library(LibOgg::LibOgg UNKNOWN IMPORTED)
  set_target_properties(
    LibOgg::LibOgg
    PROPERTIES
      IMPORTED_LOCATION "${LibOgg_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibOgg_INCLUDE_DIR}"
  )
endif()
# gersemi: on
