# Builds the plant models as FMUs (FMI 3.0 and FMI 2.0 co-simulation, binary and source), with the
# Reference FMUs framework (libs/vendor/Reference-FMUs) providing the FMI API around each model.c.
#
#   cmake -S libs/plants -B ~/build/plants -G Ninja && cmake --build ~/build/plants
#
# The FMUs land in <build>/fmus/fmi3/<name>.fmu and <build>/fmus/fmi2/<name>.fmu.
cmake_minimum_required(VERSION 3.20)
project(krumpir_plant_fmus C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REFERENCE_FMUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/Reference-FMUs"
    CACHE PATH "Reference FMUs source tree (submodule)")
if(NOT EXISTS "${REFERENCE_FMUS_DIR}/src/fmi3Functions.c")
  message(FATAL_ERROR "Reference FMUs not found in ${REFERENCE_FMUS_DIR}: run "
                      "'git submodule update --init libs/vendor/Reference-FMUs'")
endif()

# FMI platform names of the target (FMI 3.0: <arch>-<os>; FMI 2.0 uses linux64 for x86_64).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
  set(FMU_ARCH x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(FMU_ARCH aarch64)
else()
  message(FATAL_ERROR "unsupported processor ${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "the plant FMUs are built on Linux (WSL, CI, Raspberry Pi)")
endif()
set(FMU_PLATFORM_3 "${FMU_ARCH}-linux")
if(FMU_ARCH STREQUAL "x86_64")
  set(FMU_PLATFORM_2 "linux64")
else()
  set(FMU_PLATFORM_2 "aarch64-linux")
endif()

include(CTest)
find_program(KRUMPIR_PYTHON NAMES python3 HINTS "$ENV{HOME}/.venvs/krumpir/bin" NO_DEFAULT_PATH)
if(NOT KRUMPIR_PYTHON)
  find_program(KRUMPIR_PYTHON NAMES python3)
endif()

# add_plant_fmu(<name> MODEL_DIR <dir> [SOURCES <c files>] [HEADERS <h files>] [INCLUDES <dirs>]
#               [TEST <c file>])
#
# <dir> holds config.h, model.c, FMI3.xml, FMI2.xml and buildDescription.xml (FMI 3.0). SOURCES and
# HEADERS are the plant library files the model uses; they are compiled in and copied flat into the
# FMU's sources/ folder, so the source FMU builds on its own. One FMU per FMI version, each with
# the binary for this platform and the sources. A CTest test checks each FMU with FMPy. TEST is a
# C program linked statically against the FMU's sources (FMI 3.0, no function prefix), run by CTest.
# With KRUMPIR_FMU_COPY_DIR set, the FMI 3.0 FMU is also copied there (the runner uses runner/fmus).
function(add_plant_fmu name)
  cmake_parse_arguments(ARG "" "MODEL_DIR;TEST" "SOURCES;HEADERS;INCLUDES" ${ARGN})
  set(fw "${REFERENCE_FMUS_DIR}")

  if(ARG_TEST)
    add_executable(${name}_test "${ARG_TEST}" "${ARG_MODEL_DIR}/model.c" "${fw}/src/fmi3Functions.c"
                   "${fw}/src/common.c" "${fw}/src/cosimulation.c" ${ARG_SOURCES})
    target_compile_definitions(${name}_test PRIVATE FMI_VERSION=3 DISABLE_PREFIX)
    target_include_directories(${name}_test PRIVATE "${fw}/include" "${ARG_MODEL_DIR}" ${ARG_INCLUDES})
    target_compile_options(${name}_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(${name}_test PRIVATE m)
    add_test(NAME ${name}_test COMMAND ${name}_test)
  endif()

  foreach(v 3 2)
    set(target ${name}_fmi${v})
    set(stage "${CMAKE_BINARY_DIR}/stage/fmi${v}/${name}")
    set(fmu "${CMAKE_BINARY_DIR}/fmus/fmi${v}/${name}.fmu")
    set(platform ${FMU_PLATFORM_${v}})

    add_library(${target} SHARED
      "${ARG_MODEL_DIR}/model.c"
      "${fw}/src/fmi${v}Functions.c"
      "${fw}/src/common.c"
      "${fw}/src/cosimulation.c"
      ${ARG_SOURCES})
    target_compile_definitions(${target} PRIVATE FMI_VERSION=${v} DISABLE_PREFIX)
    target_include_directories(${target} PRIVATE "${fw}/include" "${ARG_MODEL_DIR}" ${ARG_INCLUDES})
    # -fno-sanitize=all: the FMU is loaded by other programs (FMPy in Python), which cannot load a
    # sanitizer-instrumented library; the runner's sanitizer build still checks the TEST program.
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -fvisibility=hidden
                                             -fno-sanitize=all)
    target_link_options(${target} PRIVATE -static-libgcc -fno-sanitize=all)
    target_link_libraries(${target} PRIVATE m)
    set_target_properties(${target} PROPERTIES
      PREFIX "" OUTPUT_NAME ${name}
      LIBRARY_OUTPUT_DIRECTORY "${stage}/binaries/${platform}")

    # Source FMU: the framework, the model and the plant library, flat in sources/
    set(src_files
      "${ARG_MODEL_DIR}/model.c" "${ARG_MODEL_DIR}/config.h"
      "${fw}/include/model.h" "${fw}/include/cosimulation.h"
      "${fw}/src/fmi${v}Functions.c" "${fw}/src/common.c" "${fw}/src/cosimulation.c"
      ${ARG_SOURCES} ${ARG_HEADERS})
    if(v EQUAL 3)
      list(APPEND src_files "${ARG_MODEL_DIR}/buildDescription.xml")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${stage}/sources"
      COMMAND ${CMAKE_COMMAND} -E copy "${ARG_MODEL_DIR}/FMI${v}.xml" "${stage}/modelDescription.xml"
      COMMAND ${CMAKE_COMMAND} -E copy ${src_files} "${stage}/sources/"
      VERBATIM)
    if(v EQUAL 2)
      # FMI 2.0 source FMUs name one file to compile (all.c, as in the Reference FMUs)
      set(all_c "${CMAKE_BINARY_DIR}/all_c/${name}/all.c")
      set(all_text "#define FMI_VERSION 2\n#include \"fmi2Functions.c\"\n#include \"model.c\"\n")
      string(APPEND all_text "#include \"common.c\"\n#include \"cosimulation.c\"\n")
      foreach(s ${ARG_SOURCES})
        get_filename_component(base "${s}" NAME)
        string(APPEND all_text "#include \"${base}\"\n")
      endforeach()
      file(WRITE "${all_c}" "${all_text}")
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${all_c}" "${stage}/sources/all.c" VERBATIM)
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/fmus/fmi${v}"
      COMMAND ${CMAKE_COMMAND} -E rm -f "${fmu}"
      COMMAND ${CMAKE_COMMAND} -E tar cf "${fmu}" --format=zip modelDescription.xml binaries sources
      WORKING_DIRECTORY "${stage}"
      COMMENT "FMU ${fmu}"
      VERBATIM)
    if(v EQUAL 3 AND KRUMPIR_FMU_COPY_DIR)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${fmu}" "${KRUMPIR_FMU_COPY_DIR}/${name}.fmu" VERBATIM)
    endif()

    if(KRUMPIR_PYTHON)
      add_test(NAME fmpy_${target}
               COMMAND ${KRUMPIR_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_fmu.py" "${fmu}")
    endif()
  endforeach()
endfunction()

set(KRUMPIR_FMU_COPY_DIR "" CACHE PATH "Also copy the FMI 3.0 FMUs to this folder")

add_plant_fmu(dcmotor
  MODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dcmotor/fmu"
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/dcmotor/src/plant_sim.c"
  HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/dcmotor/include/plant_sim.h"
  INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/dcmotor/include"
  TEST "${CMAKE_CURRENT_SOURCE_DIR}/dcmotor/fmu/test_fmu.c")

# Build check of the machinery with a Reference FMUs model (discrete, time events, no states)
option(KRUMPIR_FMU_SELFTEST "Also build the Reference FMUs model Stair through add_plant_fmu" ON)
if(KRUMPIR_FMU_SELFTEST)
  add_plant_fmu(Stair MODEL_DIR "${REFERENCE_FMUS_DIR}/Stair")
endif()
