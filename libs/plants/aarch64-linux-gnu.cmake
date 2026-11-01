# CMake toolchain for the aarch64-linux plant FMUs (Raspberry Pi 4 / 5, 64-bit OS), cross-built on
# x86_64 Linux or WSL:
#   sudo apt install gcc-aarch64-linux-gnu qemu-user
#   cmake -S libs/plants -B ~/build/plants-aarch64 -G Ninja --toolchain libs/plants/aarch64-linux-gnu.cmake
# qemu-aarch64 runs the C tests of the cross build (ctest).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64 -L /usr/aarch64-linux-gnu)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
