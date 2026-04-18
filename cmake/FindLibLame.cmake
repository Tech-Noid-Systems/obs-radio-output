# gersemi: off
find_path(
  LibLame_INCLUDE_DIR
  NAMES lame/lame.h
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    ${LibLame_ROOT}/include
    $ENV{LIBLAME_ROOT}/include
    $ENV{LIBLAME_INCLUDE}
)

if(WIN32)
  list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES .a .dll.a)
endif()

find_library(
  LibLame_LIBRARY
  NAMES mp3lame libmp3lame
  PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${LibLame_ROOT}/lib
    $ENV{LIBLAME_ROOT}/lib
    $ENV{LIBLAME_LIB}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibLame REQUIRED_VARS LibLame_LIBRARY LibLame_INCLUDE_DIR)

if(LibLame_FOUND AND NOT TARGET LibLame::LibLame)
  add_library(LibLame::LibLame UNKNOWN IMPORTED)
  set_target_properties(
    LibLame::LibLame
    PROPERTIES
      IMPORTED_LOCATION "${LibLame_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibLame_INCLUDE_DIR}"
  )
endif()
# gersemi: on
