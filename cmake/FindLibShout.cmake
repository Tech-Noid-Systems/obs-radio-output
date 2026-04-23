# gersemi: off
find_path(
  LibShout_INCLUDE_DIR
  NAMES shout/shout.h
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    ${LibShout_ROOT}/include
    $ENV{LIBSHOUT_ROOT}/include
    $ENV{LIBSHOUT_INCLUDE}
)

if(WIN32)
  list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES .a .dll.a)
elseif(APPLE)
  # macOS CI builds libshout as a Universal static archive (.a) with openssl
  # linked in; prefer the static over a .dylib so find_library picks the
  # archive when both happen to be present.
  list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

find_library(
  LibShout_LIBRARY
  NAMES shout libshout
  PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${LibShout_ROOT}/lib
    $ENV{LIBSHOUT_ROOT}/lib
    $ENV{LIBSHOUT_LIB}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibShout REQUIRED_VARS LibShout_LIBRARY LibShout_INCLUDE_DIR)

if(LibShout_FOUND AND NOT TARGET LibShout::LibShout)
  add_library(LibShout::LibShout UNKNOWN IMPORTED)
  set_target_properties(
    LibShout::LibShout
    PROPERTIES
      IMPORTED_LOCATION "${LibShout_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibShout_INCLUDE_DIR}"
  )
endif()
# gersemi: on
