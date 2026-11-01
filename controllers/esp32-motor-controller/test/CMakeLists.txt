# Host build of the controller library and its Unity tests, without PlatformIO:
#   cmake -S test -B <build> && cmake --build <build> && ctest --test-dir <build>
# (pio test -e native runs the same tests.)
cmake_minimum_required(VERSION 3.20)
project(motor_controller_tests C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra)

include(../../../libs/cmake/krumpir.cmake)
krumpir_add_libs()
add_subdirectory(../lib/control control)

enable_testing()
krumpir_unity_tests("${CMAKE_CURRENT_SOURCE_DIR}" control)
