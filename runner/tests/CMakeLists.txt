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

add_executable(test_loop_time test_loop_time.c)
target_link_libraries(test_loop_time PRIVATE krumpir_core)
add_test(NAME loop_time COMMAND test_loop_time)

add_executable(test_io_null test_io_null.c)
target_link_libraries(test_io_null PRIVATE krumpir_core)
add_test(NAME io_null COMMAND test_io_null)

add_executable(test_runner test_runner.c)
target_link_libraries(test_runner PRIVATE krumpir_core)
target_compile_definitions(test_runner PRIVATE KRUMPIR_FMU_DIR="${PROJECT_SOURCE_DIR}/fmus")
add_test(NAME runner COMMAND test_runner)
set_tests_properties(runner PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_writer test_writer.c)
target_link_libraries(test_writer PRIVATE krumpir_core)
add_test(NAME writer COMMAND test_writer)
