# Tier 1 unit-test harness (Catch2 v3 + CTest).  Added to the build only when
# ENABLE_TESTING is ON (see the root CMakeLists.txt); the production plugin
# never links any of these targets.  Decision context: the automated-testing
# proposal in the private working folder; tracking issue #73.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Umbrella target so CI can build every test executable with a single
# `cmake --build --target radio-tests`.  This matters on the macOS Xcode
# generator, where the packaging build only builds the plugin target — the test
# executables would otherwise never be compiled.  Each radio_add_test() call
# attaches its executable here.
add_custom_target(radio-tests)

# radio_add_test(<name> <sources...>) — define a Catch2 test executable,
# register its cases with CTest, and wire it into the radio-tests umbrella.
function(radio_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain)
  if(APPLE)
    # Catch2 from Homebrew is a single-architecture (host) build.  The plugin
    # is Universal (arm64;x86_64), but linking a Universal test executable
    # against single-arch Catch2 fails the absent slice.  Test binaries only
    # ever run on the build/CI machine and never ship, so pin them to the host
    # architecture.
    set_target_properties(${name} PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
  # PRE_TEST discovery defers test enumeration to `ctest` run time instead of
  # running the binary as a post-build step — more robust under the Xcode
  # multi-config generator.
  catch_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
  add_dependencies(radio-tests ${name})
endfunction()

radio_add_test(test_smoke test_smoke.cpp)

# OpusHead / OpusTags byte-layout test (#75).  Compiles only the pure
# serializer TU — no libopus / libogg / OBS — so no extra include trees or
# stubs are needed.
radio_add_test(test_opus_headers test_opus_headers.cpp ${CMAKE_SOURCE_DIR}/src/ogg-opus-headers.c)
target_include_directories(test_opus_headers PRIVATE ${CMAKE_SOURCE_DIR}/src)
