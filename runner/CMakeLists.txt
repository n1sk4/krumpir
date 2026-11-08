# krumpir: the FMU runner (CLI `krumpir run|serve|info|init`) and its tests, C11, Linux / WSL / Pi:
#   cmake -S runner -B <build> -G Ninja && cmake --build <build> && ctest --test-dir <build>
# Also builds the plant FMUs into fmus/ (libs/plants, with the Reference-FMUs submodule) and the
# controllers for the PC (controllers/*/host, KRUMPIR_HIL_PC) for the closed-loop tests.
cmake_minimum_required(VERSION 3.25)
project(krumpir C)
include(../cmake/krumpir.cmake)

find_package(Threads REQUIRED)
find_package(EXPAT REQUIRED)

# Vendored: a TOML parser, a zip reader, a JSON library for the service, the FMI 3.0 headers
add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC vendor/tomlc99)
krumpir_vendor(tomlc99)
add_library(miniz STATIC vendor/miniz/miniz.c)
target_include_directories(miniz PUBLIC vendor/miniz)
# Drops the zlib-style wrappers (static functions in miniz.h that -Wunused-function flags).
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_APIS)
# ftello/fseeko are POSIX: hidden in strict C11, an error since GCC 14 (implicit declaration)
target_compile_definitions(miniz PRIVATE _POSIX_C_SOURCE=200809L)
krumpir_vendor(miniz)
add_library(cjson STATIC vendor/cjson/cJSON.c)
target_include_directories(cjson PUBLIC vendor/cjson)
krumpir_vendor(cjson)

# The HIL link frames (libs/hil), shared with the controllers
krumpir_add_libs()

set(srcs
  src/app/config.c
  src/app/scaffold.c
  src/core/cmdq.c
  src/core/lifecycle.c
  src/core/loop.c
  src/core/platform.c
  src/core/rt_setup.c
  src/core/runner.c
  src/core/session.c
  src/core/siggen.c
  src/core/timing_pin.c
  src/fmi/fmi3.c
  src/fmi/loader.c
  src/fmi/modeldesc.c
  src/fmi/varmap.c
  src/io/io.c
  src/io/null.c
  src/io/uart.c
  src/telemetry/capture.c
  src/telemetry/ring.c
  src/telemetry/writer.c)
add_library(krumpir_core STATIC ${srcs})
add_library(krumpir::core ALIAS krumpir_core)
target_include_directories(krumpir_core PUBLIC src/app src/core src/fmi src/io src/telemetry)
target_include_directories(krumpir_core SYSTEM PUBLIC vendor/fmi3) # FMI 3.0 headers (BSD-2)
# Strict C11 hides POSIX; the runner needs clock_nanosleep, nanosleep, termios, mkstemp, nftw.
target_compile_definitions(krumpir_core PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(krumpir_core PUBLIC Threads::Threads tomlc99 miniz EXPAT::EXPAT hil
                                          ${CMAKE_DL_LIBS} m
                                   PRIVATE krumpir::warnings)

add_executable(krumpir src/app/main.c src/app/serve.c)
target_link_libraries(krumpir PRIVATE krumpir_core cjson krumpir::warnings)

# The Reference FMUs for the tests (cmake/versions.cmake), into fmus/ when they are missing; no
# network: a warning, and the tests that need them skip
file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/fmus") # not in git: the FMUs only
option(KRUMPIR_FETCH_FMUS "Download the Reference FMUs into fmus/ when they are missing" ON)
if(KRUMPIR_FETCH_FMUS)
  krumpir_reference_fmus("${PROJECT_SOURCE_DIR}/fmus" OPTIONAL)
endif()

# The plant FMUs, copied to fmus/ for the scenarios and tests
if(EXISTS "${KRUMPIR_LIBS_DIR}/vendor/Reference-FMUs/src/fmi3Functions.c")
  set(KRUMPIR_FMU_COPY_DIR "${PROJECT_SOURCE_DIR}/fmus" CACHE PATH "" FORCE)
  set(KRUMPIR_FMU_SELFTEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(../libs/plants plants)
else()
  message(STATUS "Plant FMUs not built: run 'git submodule update --init libs/vendor/Reference-FMUs'")
endif()

# The controllers built for this PC, for the closed-loop tests over a pty pair (tools/hil)
option(KRUMPIR_HIL_PC "Build the controllers for the PC and the closed-loop tests" ON)
if(KRUMPIR_HIL_PC)
  foreach(c motor t12)
    add_subdirectory(../controllers/esp32-${c}-controller/host host-${c})
  endforeach()
endif()

enable_testing()
add_subdirectory(tests)
