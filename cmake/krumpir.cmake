# The repository's CMake conventions, included by every CMake project here (include_guard: once).
#
#   include(<repo>/cmake/krumpir.cmake)
#
# Gives:
#   C11 and C++17, no compiler extensions, compile_commands.json
#   krumpir::warnings          -Wall -Wextra -Werror (+ -Wpedantic for C++); MSVC /W4
#   krumpir::warnings_relaxed  the same with -Wno-unused-parameter (the board's hook signatures)
#   KRUMPIR_SANITIZE           OFF | address | thread (ON = address): every C/C++ target here
#   krumpir_add_libs()         the portable libraries: ctrlcore, dcmotor, t12, osal, osal_host
#                              (alias osal_port), rte, hil, unity; each also as krumpir::<name>
#   krumpir_unity_tests(<dir> <libs...>)   each <dir>/test_*/test_main.c: one CTest test
#   krumpir_add_test(NAME <n> SOURCES <c...> | COMMAND <cmd...>
#                    [LIBS <targets>] [DEFINES <defs>] [ENV <k=v...>] [WORKDIR <dir>] [SKIP_77])
#   krumpir_vendor(<target>)   third-party code: no warnings of ours
#
# Each library's CMakeLists.txt also works as an ESP-IDF component; PlatformIO builds them from
# library.json.
include_guard(GLOBAL)

get_filename_component(KRUMPIR_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(KRUMPIR_LIBS_DIR "${KRUMPIR_ROOT}/libs")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings: linked PRIVATE by each of our targets, never added globally, so vendored code and the
# projects that include ours keep their own.
if(NOT TARGET krumpir_warnings)
  add_library(krumpir_warnings INTERFACE)
  add_library(krumpir::warnings ALIAS krumpir_warnings)
  if(MSVC)
    target_compile_options(krumpir_warnings INTERFACE /W4 /permissive- /utf-8)
  else()
    target_compile_options(krumpir_warnings INTERFACE -Wall -Wextra -Werror
                                                      $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
  endif()
  add_library(krumpir_warnings_relaxed INTERFACE)
  add_library(krumpir::warnings_relaxed ALIAS krumpir_warnings_relaxed)
  target_link_libraries(krumpir_warnings_relaxed INTERFACE krumpir_warnings)
  if(NOT MSVC)
    target_compile_options(krumpir_warnings_relaxed INTERFACE -Wno-unused-parameter)
  endif()
endif()

# Sanitizers, for everything built in this tree (ON is address, as the runner's CI passes it).
set(KRUMPIR_SANITIZE OFF CACHE STRING "Sanitizer: OFF, address (also ON) or thread")
set_property(CACHE KRUMPIR_SANITIZE PROPERTY STRINGS OFF address thread)
if(KRUMPIR_SANITIZE STREQUAL "ON" OR KRUMPIR_SANITIZE STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif(KRUMPIR_SANITIZE STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

macro(krumpir_add_libs)
  foreach(_lib ctrlcore plants/dcmotor plants/t12 osal osal_host rte hil)
    get_filename_component(_name ${_lib} NAME)
    if(NOT TARGET ${_name})
      add_subdirectory("${KRUMPIR_LIBS_DIR}/${_lib}" "${CMAKE_BINARY_DIR}/krumpir_libs/${_name}")
    endif()
  endforeach()
  if(NOT TARGET unity)
    add_library(unity STATIC "${KRUMPIR_LIBS_DIR}/vendor/unity/unity.c")
    target_include_directories(unity PUBLIC "${KRUMPIR_LIBS_DIR}/vendor/unity")
    krumpir_vendor(unity)
  endif()
endmacro()

function(krumpir_vendor target)
  set_target_properties(${target} PROPERTIES SYSTEM ON)
  get_target_property(_type ${target} TYPE)
  if(NOT _type STREQUAL "INTERFACE_LIBRARY")
    target_compile_options(${target} PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
  endif()
endfunction()

function(krumpir_unity_tests dir)
  file(GLOB _tests LIST_DIRECTORIES true "${dir}/test_*")
  foreach(_t ${_tests})
    if(EXISTS "${_t}/test_main.c")
      get_filename_component(_name "${_t}" NAME)
      add_executable(${_name} "${_t}/test_main.c")
      target_include_directories(${_name} PRIVATE "${_t}") # test data headers next to the test
      target_link_libraries(${_name} PRIVATE unity ${ARGN} krumpir::warnings)
      add_test(NAME ${_name} COMMAND ${_name})
    endif()
  endforeach()
endfunction()

function(krumpir_add_test)
  cmake_parse_arguments(ARG "SKIP_77" "NAME;WORKDIR" "SOURCES;COMMAND;LIBS;DEFINES;ENV" ${ARGN})
  if(ARG_SOURCES)
    set(_exe test_${ARG_NAME})
    add_executable(${_exe} ${ARG_SOURCES})
    target_link_libraries(${_exe} PRIVATE ${ARG_LIBS} krumpir::warnings)
    target_compile_definitions(${_exe} PRIVATE ${ARG_DEFINES})
    set(ARG_COMMAND ${_exe} ${ARG_COMMAND})
  endif()
  if(ARG_WORKDIR)
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_COMMAND} WORKING_DIRECTORY ${ARG_WORKDIR})
  else()
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_COMMAND})
  endif()
  if(ARG_SKIP_77)
    set_tests_properties(${ARG_NAME} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(ARG_ENV)
    set_tests_properties(${ARG_NAME} PROPERTIES ENVIRONMENT "${ARG_ENV}")
  endif()
endfunction()
