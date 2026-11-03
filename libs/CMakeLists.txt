# krumpir_libs: the shared libraries and their Unity tests (libs/test), without PlatformIO:
#   cmake -S libs -B <build> && cmake --build <build> && ctest --test-dir <build>
# The plant FMUs are a separate build: plants/CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
project(krumpir_libs C)
include(../cmake/krumpir.cmake)

krumpir_add_libs()

enable_testing()
krumpir_unity_tests("${CMAKE_CURRENT_SOURCE_DIR}/test" ctrlcore dcmotor t12 rte osal_host hil)
