# Host build of the shared libraries and their Unity tests (libs/test), without PlatformIO:
#   cmake -S libs -B <build> && cmake --build <build> && ctest --test-dir <build>
# The plant FMUs are a separate build: plants/CMakeLists.txt.
cmake_minimum_required(VERSION 3.20)
project(krumpir_libs C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra)

include(cmake/krumpir.cmake)
krumpir_add_libs()

enable_testing()
krumpir_unity_tests("${CMAKE_CURRENT_SOURCE_DIR}/test" ctrlcore dcmotor t12 rte osal_host)
