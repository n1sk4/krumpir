# krumpir_repo: everything in this repository that builds on a PC, in one tree (the board firmware
# is PlatformIO's: pio run in controllers/<x>). Use the presets (CMakePresets.json):
#   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
# The build goes to build/<preset>; on WSL with the repository on /mnt/<drive>, put it on the Linux
# side instead, which builds much faster: cmake --preset dev -B ~/build/dev, then
# cmake --build ~/build/dev && ctest --test-dir ~/build/dev.
# Presets: dev, release, asan, tsan (Linux), pi-cross (the plant FMUs for aarch64), mingw (Windows).
# Parts, each also buildable on its own (cmake -S <dir>):
#   libs         the shared libraries and their Unity tests (libs/)
#   controllers  each controller's library and Unity tests (controllers/*/test)
#   runner       the runner, the plant FMUs and the controllers for the PC (runner/; Linux)
#   plants       the plant FMUs alone (libs/plants; Linux), for a cross build without the runner
#   tui          the terminal UI (clients/tui)
# KRUMPIR_ONLY limits the tree to some of them (e.g. "runner;tui"); empty: all this platform builds.
cmake_minimum_required(VERSION 3.25)
project(krumpir_repo C)
include(cmake/krumpir.cmake)

set(KRUMPIR_ONLY "" CACHE STRING "Parts to build: libs, controllers, runner, plants, tui (empty: all)")
function(krumpir_part name out)
  if(KRUMPIR_ONLY STREQUAL "" OR name IN_LIST KRUMPIR_ONLY)
    set(${out} ON PARENT_SCOPE)
  else()
    set(${out} OFF PARENT_SCOPE)
  endif()
endfunction()
krumpir_part(libs want_libs)
krumpir_part(controllers want_controllers)
krumpir_part(runner want_runner)
krumpir_part(plants want_plants)
krumpir_part(tui want_tui)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(want_runner OFF) # POSIX, termios, PREEMPT_RT
  set(want_plants OFF) # FMU binaries for Linux
endif()
if(want_runner)
  set(want_plants OFF) # the runner builds them
endif()

enable_testing()
if(want_libs)
  add_subdirectory(libs)
endif()
if(want_controllers)
  file(GLOB tests LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/controllers/*/test/CMakeLists.txt")
  foreach(t ${tests})
    get_filename_component(dir "${t}" DIRECTORY)
    get_filename_component(ctrl "${dir}" DIRECTORY)
    get_filename_component(ctrl "${ctrl}" NAME)
    add_subdirectory("${dir}" "controllers/${ctrl}")
  endforeach()
endif()
if(want_runner)
  add_subdirectory(runner)
endif()
if(want_plants)
  add_subdirectory(libs/plants plants)
endif()
if(want_tui)
  enable_language(CXX)
  add_subdirectory(clients/tui)
endif()

message(STATUS "krumpir: libs ${want_libs}, controllers ${want_controllers}, runner ${want_runner}, "
               "plants ${want_plants}, tui ${want_tui}")
