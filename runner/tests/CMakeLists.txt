add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)

add_executable(test_ring test_ring.c)
target_link_libraries(test_ring PRIVATE krumpir_core)
add_test(NAME ring COMMAND test_ring)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE krumpir_core)
add_test(NAME config COMMAND test_config)

add_executable(test_lifecycle test_lifecycle.c)
target_link_libraries(test_lifecycle PRIVATE krumpir_core)
add_test(NAME lifecycle COMMAND test_lifecycle)

add_executable(test_modeldesc test_modeldesc.c)
target_link_libraries(test_modeldesc PRIVATE krumpir_core)
add_test(NAME modeldesc COMMAND test_modeldesc)

add_executable(test_varmap test_varmap.c)
target_link_libraries(test_varmap PRIVATE krumpir_core)
add_test(NAME varmap COMMAND test_varmap)

add_executable(test_loader test_loader.c)
target_link_libraries(test_loader PRIVATE krumpir_core)
add_test(NAME loader COMMAND test_loader)

# Needs the Reference FMUs (runner/fmus/README.md); reports SKIPPED when they are missing.
add_executable(test_loader_fmus test_loader_fmus.c)
target_link_libraries(test_loader_fmus PRIVATE krumpir_core)
target_compile_definitions(test_loader_fmus PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME loader_fmus COMMAND test_loader_fmus)
set_tests_properties(loader_fmus PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_fmi3 test_fmi3.c)
target_link_libraries(test_fmi3 PRIVATE krumpir_core)
target_compile_definitions(test_fmi3 PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME fmi3 COMMAND test_fmi3)
set_tests_properties(fmi3 PROPERTIES SKIP_RETURN_CODE 77)

# krumpir init: every FMU's starter scenario parses and runs; SKIPPED without the FMUs
add_executable(test_scaffold test_scaffold.c)
target_link_libraries(test_scaffold PRIVATE krumpir_core)
target_compile_definitions(test_scaffold PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME scaffold COMMAND test_scaffold)
set_tests_properties(scaffold PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_loop_time test_loop_time.c)
target_link_libraries(test_loop_time PRIVATE krumpir_core)
add_test(NAME loop_time COMMAND test_loop_time)

add_executable(test_io_null test_io_null.c)
target_link_libraries(test_io_null PRIVATE krumpir_core)
add_test(NAME io_null COMMAND test_io_null)

# The uart backend against a fake controller on a pty pair
add_executable(test_io_uart test_io_uart.c)
target_link_libraries(test_io_uart PRIVATE krumpir_core)
add_test(NAME io_uart COMMAND test_io_uart)

add_executable(test_signal test_signal.c)
target_link_libraries(test_signal PRIVATE krumpir_core)
add_test(NAME signal COMMAND test_signal)

add_executable(test_capture test_capture.c)
target_link_libraries(test_capture PRIVATE krumpir_core)
add_test(NAME capture COMMAND test_capture)

add_executable(test_runner test_runner.c)
target_link_libraries(test_runner PRIVATE krumpir_core)
target_compile_definitions(test_runner PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME runner COMMAND test_runner)
set_tests_properties(runner PROPERTIES SKIP_RETURN_CODE 77)

# FMPy reference comparison (tools/compare_fmpy.py): runs each example scenario and checks the log
# against FMPy. Needs the Python venv from tools/setup_wsl.sh; reports SKIPPED if the FMUs are
# missing.
set(KRUMPIR_PYTHON "$ENV{HOME}/.venvs/krumpir/bin/python" CACHE FILEPATH "Python with fmpy")
if(EXISTS "${KRUMPIR_PYTHON}")
  foreach(name feedthrough dahlquist vanderpol dcmotor t12tip)
    add_test(NAME fmpy_${name}
      COMMAND ${KRUMPIR_PYTHON} ${PROJECT_SOURCE_DIR}/tools/compare_fmpy.py scenarios/${name}.toml
              --run $<TARGET_FILE:krumpir>
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(fmpy_${name} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()
  # The Python client (tools/krumpir) against this build's service, if pytest is installed
  execute_process(COMMAND ${KRUMPIR_PYTHON} -c "import pytest" RESULT_VARIABLE no_pytest
                  OUTPUT_QUIET ERROR_QUIET)
  if(no_pytest EQUAL 0)
    add_test(NAME client
      COMMAND ${KRUMPIR_PYTHON} -m pytest -q ${PROJECT_SOURCE_DIR}/../tools/krumpir/tests
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/../tools/krumpir)
    set_tests_properties(client PROPERTIES ENVIRONMENT
      "KRUMPIR_EXE=$<TARGET_FILE:krumpir>;PYTHONPATH=${PROJECT_SOURCE_DIR}/../tools/krumpir")
  else()
    message(STATUS "Client tests not added: pytest not found in ${KRUMPIR_PYTHON}")
  endif()
else()
  message(STATUS "FMPy comparison tests not added: ${KRUMPIR_PYTHON} not found (tools/setup_wsl.sh)")
endif()

# Golden files (tests/golden): the deterministic columns of short runs, compared with a tolerance.
# Regenerate after an intended change with:
#   cd runner && python3 tools/check_golden.py <build>/krumpir tests/golden/<name>.toml
#                tests/golden/<name>.csv --update
find_package(Python3 3.11 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  foreach(name feedthrough dahlquist vanderpol dcmotor t12tip)
    add_test(NAME golden_${name}
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_golden.py
              $<TARGET_FILE:krumpir> tests/golden/${name}.toml tests/golden/${name}.csv
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(golden_${name} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()
  # The model-access service over TCP (krumpir serve)
  add_test(NAME serve
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_serve.py $<TARGET_FILE:krumpir>
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(serve PROPERTIES SKIP_RETURN_CODE 77)
else()
  message(STATUS "Golden and service tests not added: Python 3.11+ not found")
endif()

# The closed loop on this PC: each controller's own code against its plant FMU over a pty pair,
# checked against the controller's acceptance figures. SKIPPED without socat (apt install socat).
if(KRUMPIR_HIL_PC AND Python3_Interpreter_FOUND)
  # and the faults: each must trip the controller as named in check_loop.py, or fault the run
  foreach(kind motor motor-sensor motor-link t12 t12-open t12-short t12-stuck t12-runaway)
    string(REGEX REPLACE "-.*$" "" ctrl ${kind})
    add_test(NAME hil_pc_${kind}
      COMMAND bash ${PROJECT_SOURCE_DIR}/../tools/hil/pc_loop.sh ${kind} --check)
    set_tests_properties(hil_pc_${kind} PROPERTIES SKIP_RETURN_CODE 77 RESOURCE_LOCK hil_pty
      ENVIRONMENT "KRUMPIR=$<TARGET_FILE:krumpir>;CONTROLLER=$<TARGET_FILE:${ctrl}_host>")
  endforeach()
  # SIL vs PC loop: every closed-loop scenario on the controller's on-chip plant and as the PC
  # loop, compared signal by signal (tools/hil/compare.py; the report: compare.py without --check)
  foreach(ctrl motor t12)
    add_test(NAME hil_compare_${ctrl}
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/../tools/hil/compare.py ${ctrl}
              --build ${PROJECT_BINARY_DIR} --check --no-plots)
    set_tests_properties(hil_compare_${ctrl} PROPERTIES SKIP_RETURN_CODE 77
      RESOURCE_LOCK hil_pty)
  endforeach()
endif()

add_executable(test_writer test_writer.c)
target_link_libraries(test_writer PRIVATE krumpir_core)
add_test(NAME writer COMMAND test_writer)

add_executable(test_session test_session.c)
target_link_libraries(test_session PRIVATE krumpir_core)
target_compile_definitions(test_session PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME session COMMAND test_session)
set_tests_properties(session PROPERTIES SKIP_RETURN_CODE 77)

# SCHED_FIFO, pinning and mlockall: needs root or rtprio/memlock limits, else SKIPPED
add_executable(test_rt_setup test_rt_setup.c)
target_link_libraries(test_rt_setup PRIVATE krumpir_core)
target_compile_definitions(test_rt_setup PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME rt_setup COMMAND test_rt_setup)
set_tests_properties(rt_setup PROPERTIES SKIP_RETURN_CODE 77)
