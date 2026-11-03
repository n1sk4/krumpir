# krumpir_tui: the terminal UI (krumpir-tui) for the runner and the controllers, C++17, Windows
# (MinGW, MSVC), Linux and the Pi:
#   cmake -S clients/tui -B <build> -DCMAKE_BUILD_TYPE=Release && cmake --build <build>
# FTXUI, Asio, nlohmann/json and doctest are fetched at pinned versions.
cmake_minimum_required(VERSION 3.25) # FetchContent SYSTEM
project(krumpir_tui CXX)
include(../../cmake/krumpir.cmake)

# Dependencies, pinned by version and hash. Their warnings stay theirs (SYSTEM).
include(FetchContent)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET ON CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui SYSTEM
  URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v7.0.3.tar.gz
  URL_HASH SHA256=e7c62ffe19009759821b4f0f8df7f2a6fb83784c3a9f1477d81f56d3ee723c88)
FetchContent_Declare(asio SYSTEM
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
  URL_HASH SHA256=755bd7f85a4b269c67ae0ea254907c078d408cce8e1a352ad2ed664d233780e8)
FetchContent_Declare(json SYSTEM
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
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
  FetchContent_Declare(doctest SYSTEM
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
    SOURCE_SUBDIR no-cmake)
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
