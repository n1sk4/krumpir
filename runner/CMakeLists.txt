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

find_package(EXPAT REQUIRED)

add_library(krumpir_core STATIC
  src/app/config.c
  src/core/lifecycle.c
  src/core/loop.c
  src/core/platform.c
  src/core/runner.c
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

add_executable(krumpir src/app/main.c)
target_link_libraries(krumpir PRIVATE krumpir_core)

enable_testing()
add_subdirectory(tests)
