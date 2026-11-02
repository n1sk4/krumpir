cmake_minimum_required(VERSION 3.20)
project(krumpir C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Werror)
# Strict -std=c11 hides POSIX; the runner needs clock_nanosleep, nanosleep, termios, mkstemp,
# nftw. _XOPEN_SOURCE=700 includes POSIX.1-2008.
add_compile_definitions(_XOPEN_SOURCE=700)

option(KRUMPIR_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(KRUMPIR_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# Vendored TOML parser, built without our warning flags.
add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC vendor/tomlc99)
target_compile_options(tomlc99 PRIVATE -w)

# Vendored zip reader, built without our warning flags.
add_library(miniz STATIC vendor/miniz/miniz.c)
target_include_directories(miniz PUBLIC vendor/miniz)
target_compile_options(miniz PRIVATE -w)
# Drops the zlib-style wrappers (static functions in miniz.h that -Wunused-function flags).
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_APIS)

# Vendored JSON library for the service, built without our warning flags.
add_library(cjson STATIC vendor/cjson/cJSON.c)
target_include_directories(cjson PUBLIC vendor/cjson)
target_compile_options(cjson PRIVATE -w)

find_package(EXPAT REQUIRED)

add_library(krumpir_core STATIC
  src/app/config.c
  src/app/scaffold.c
  src/core/lifecycle.c
  src/core/loop.c
  src/core/platform.c
  src/core/cmdq.c
  src/core/rt_setup.c
  src/core/runner.c
  src/core/session.c
  src/core/siggen.c
  src/telemetry/capture.c
  src/fmi/fmi3.c
  src/fmi/loader.c
  src/fmi/modeldesc.c
  src/fmi/varmap.c
  src/io/io.c
  src/io/null.c
  src/telemetry/ring.c
  src/telemetry/writer.c)
target_include_directories(krumpir_core PUBLIC src/app src/core src/fmi src/io src/telemetry)
# FMI 3.0 headers (BSD-2), vendored; SYSTEM keeps their warnings out of our -Werror build.
target_include_directories(krumpir_core SYSTEM PUBLIC vendor/fmi3)
target_link_libraries(krumpir_core PUBLIC Threads::Threads tomlc99 miniz EXPAT::EXPAT
  ${CMAKE_DL_LIBS} m)

add_executable(krumpir src/app/main.c src/app/serve.c)
target_link_libraries(krumpir PRIVATE krumpir_core cjson)

# Plant FMUs (libs/plants), built with the runner and copied to fmus/ for the scenarios and tests.
if(EXISTS "${PROJECT_SOURCE_DIR}/../libs/vendor/Reference-FMUs/src/fmi3Functions.c")
  set(KRUMPIR_FMU_COPY_DIR "${PROJECT_SOURCE_DIR}/fmus" CACHE PATH "" FORCE)
  set(KRUMPIR_FMU_SELFTEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(../libs/plants plants)
else()
  message(STATUS "Plant FMUs not built: run 'git submodule update --init libs/vendor/Reference-FMUs'")
endif()

enable_testing()
add_subdirectory(tests)
