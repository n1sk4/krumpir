# krumpir_tui: the terminal UI (krumpir-tui) for the runner and the controllers, C++17, Windows
# (MinGW, MSVC), Linux and the Pi:
#   cmake -S clients/tui -B <build> -DCMAKE_BUILD_TYPE=Release && cmake --build <build>
# FTXUI, Asio, nlohmann/json and doctest are fetched at pinned versions (cmake/versions.cmake).
cmake_minimum_required(VERSION 3.25) # FetchContent SYSTEM
project(krumpir_tui CXX)
include(../../cmake/krumpir.cmake)

# Dependencies, pinned in cmake/versions.cmake and downloaded once into KRUMPIR_CACHE. Their
# warnings stay theirs (SYSTEM).
include(FetchContent)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET ON CACHE BOOL "" FORCE)
macro(krumpir_tui_dep name var)
  krumpir_download(${KRUMPIR_${var}_URL} ${KRUMPIR_${var}_SHA256} _archive)
  FetchContent_Declare(${name} SYSTEM URL "${_archive}" URL_HASH SHA256=${KRUMPIR_${var}_SHA256}
                       ${ARGN})
endmacro()
krumpir_tui_dep(ftxui FTXUI)
krumpir_tui_dep(asio ASIO)
krumpir_tui_dep(json JSON)
FetchContent_MakeAvailable(ftxui asio json)

# Standalone Asio is header-only and has no CMake project of its own.
add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
  target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
  target_link_libraries(asio INTERFACE ws2_32 mswsock)
endif()

if(MINGW)
  # One .exe without libstdc++/libgcc DLLs (another MinGW's DLLs on PATH would not match).
  add_link_options(-static)
endif()

# The client and model: no UI, so the tests and later tools can use them.
find_package(Threads REQUIRED)
set(core_srcs
  src/a2l.cpp
  src/ecu_model.cpp
  src/form.cpp
  src/layout.cpp
  src/model.cpp
  src/service.cpp
  src/xcp.cpp)
add_library(krumpir_tui_core STATIC ${core_srcs})
target_include_directories(krumpir_tui_core PUBLIC src)
target_link_libraries(krumpir_tui_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads
                                       PRIVATE asio krumpir::warnings)

# The screens: a library of their own, so the tests can draw them without a terminal.
set(ui_srcs
  src/ui/controller.cpp
  src/ui/load.cpp
  src/ui/monitor.cpp
  src/ui/plot.cpp
  src/ui/root.cpp
  src/ui/split.cpp)
add_library(krumpir_tui_ui STATIC ${ui_srcs})
target_include_directories(krumpir_tui_ui PUBLIC src/ui)
target_link_libraries(krumpir_tui_ui PUBLIC krumpir_tui_core ftxui::component ftxui::dom
                                            ftxui::screen
                                     PRIVATE krumpir::warnings)

add_executable(krumpir-tui src/main.cpp)
target_link_libraries(krumpir-tui PRIVATE krumpir_tui_ui krumpir::warnings)

include(CTest)
if(BUILD_TESTING)
  # doctest is header-only: its sources only (SOURCE_SUBDIR points at no CMakeLists.txt), because
  # its own CMakeLists asks for a CMake older than 3.5, which CMake 4 refuses.
  krumpir_tui_dep(doctest DOCTEST SOURCE_SUBDIR no-cmake)
  FetchContent_MakeAvailable(doctest)
  add_library(doctest INTERFACE)
  target_include_directories(doctest SYSTEM INTERFACE ${doctest_SOURCE_DIR})

  set(test_srcs
    tests/test_core.cpp
    tests/test_ecu.cpp
    tests/test_form.cpp
    tests/test_layout.cpp
    tests/test_ui.cpp)
  add_executable(test_core ${test_srcs})
  target_compile_definitions(test_core PRIVATE KT_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
  target_link_libraries(test_core PRIVATE krumpir_tui_ui asio doctest krumpir::warnings)
  add_test(NAME core COMMAND test_core)
endif()
