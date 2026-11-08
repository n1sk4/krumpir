# The runner's tests: C unit and integration tests, the FMPy and golden-file comparisons, the
# service, the Python client, and the closed loops with the controllers on this PC. A test that
# needs something this machine lacks (FMUs, root, socat) reports SKIPPED (exit 77).
set(fmus KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")

foreach(t ring config lifecycle modeldesc varmap loader loop_time io_null io_uart signal capture
          writer)
  krumpir_add_test(NAME ${t} SOURCES test_${t}.c LIBS krumpir_core)
endforeach()

# Need the FMUs (runner/fmus, built with the runner and fetched when it is configured); the last
# also root or rtprio/memlock limits (SCHED_FIFO, pinning, mlockall)
foreach(t loader_fmus fmi3 scaffold runner session rt_setup)
  krumpir_add_test(NAME ${t} SOURCES test_${t}.c LIBS krumpir_core DEFINES ${fmus} SKIP_77)
endforeach()

# FMPy reference comparison (tools/compare_fmpy.py) and the Python client (tools/krumpir): need the
# venv from tools/kb.py setup
set(KRUMPIR_PYTHON "$ENV{HOME}/.venvs/krumpir/bin/python" CACHE FILEPATH "Python with fmpy")
if(EXISTS "${KRUMPIR_PYTHON}")
  foreach(name feedthrough dahlquist vanderpol dcmotor t12tip)
    krumpir_add_test(NAME fmpy_${name} WORKDIR ${PROJECT_SOURCE_DIR} SKIP_77
      COMMAND ${KRUMPIR_PYTHON} ${PROJECT_SOURCE_DIR}/tools/compare_fmpy.py
              scenarios/${name}.toml --run $<TARGET_FILE:krumpir>)
  endforeach()
  execute_process(COMMAND ${KRUMPIR_PYTHON} -c "import pytest" RESULT_VARIABLE no_pytest
                  OUTPUT_QUIET ERROR_QUIET)
  if(no_pytest EQUAL 0)
    krumpir_add_test(NAME client WORKDIR ${PROJECT_SOURCE_DIR}/../tools/krumpir
      COMMAND ${KRUMPIR_PYTHON} -m pytest -q ${PROJECT_SOURCE_DIR}/../tools/krumpir/tests
      ENV "KRUMPIR_EXE=$<TARGET_FILE:krumpir>;PYTHONPATH=${PROJECT_SOURCE_DIR}/../tools/krumpir")
  else()
    message(STATUS "Client tests not added: pytest not found in ${KRUMPIR_PYTHON}")
  endif()
else()
  message(STATUS "FMPy comparison tests not added: ${KRUMPIR_PYTHON} not found (python3 tools/kb.py setup)")
endif()

# Golden files (tests/golden), the service over TCP, and the closed loops. Regenerate a golden file
# after an intended change: cd runner && python3 tools/check_golden.py <build>/krumpir
#   tests/golden/<name>.toml tests/golden/<name>.csv --update
find_package(Python3 3.13 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(STATUS "Golden, service and closed-loop tests not added: Python 3.13+ not found")
  return()
endif()
foreach(name feedthrough dahlquist vanderpol dcmotor t12tip)
  krumpir_add_test(NAME golden_${name} WORKDIR ${PROJECT_SOURCE_DIR} SKIP_77
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_golden.py
            $<TARGET_FILE:krumpir> tests/golden/${name}.toml tests/golden/${name}.csv)
endforeach()
krumpir_add_test(NAME serve WORKDIR ${PROJECT_SOURCE_DIR} SKIP_77
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_serve.py $<TARGET_FILE:krumpir>)

if(KRUMPIR_HIL_PC)
  # Each controller's own code against its plant FMU over a pty pair (tools/hil/pc_loop.sh),
  # checked against its acceptance figures, and the faults (check_loop.py); needs socat
  foreach(kind motor motor-sensor motor-stall motor-link t12 t12-open t12-short t12-stuck
               t12-runaway)
    string(REGEX REPLACE "-.*$" "" ctrl ${kind})
    krumpir_add_test(NAME hil_pc_${kind} SKIP_77
      COMMAND bash ${PROJECT_SOURCE_DIR}/../tools/hil/pc_loop.sh ${kind} --check
      ENV "KRUMPIR_RUNNER=$<TARGET_FILE:krumpir>;KRUMPIR_CONTROLLER=$<TARGET_FILE:${ctrl}_host>")
    set_tests_properties(hil_pc_${kind} PROPERTIES RESOURCE_LOCK hil_pty)
  endforeach()
  # SIL vs PC loop, signal by signal (tools/hil/compare.py; the report: compare.py without --check)
  foreach(ctrl motor t12)
    krumpir_add_test(NAME hil_compare_${ctrl} SKIP_77
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/../tools/hil/compare.py ${ctrl}
              --build ${PROJECT_BINARY_DIR} --check --no-plots)
    set_tests_properties(hil_compare_${ctrl} PROPERTIES RESOURCE_LOCK hil_pty)
  endforeach()
endif()
