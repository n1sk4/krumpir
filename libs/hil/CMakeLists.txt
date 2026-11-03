# hil: HIL link frames (runner <-> controller over a serial line). Plain C, no OS. Built by any
# CMake project (add_subdirectory), as an ESP-IDF component, or by PlatformIO through library.json.
if(ESP_PLATFORM)
  idf_component_register(SRCS src/hil.c INCLUDE_DIRS include)
  return()
endif()

add_library(hil STATIC src/hil.c)
target_include_directories(hil PUBLIC include)
