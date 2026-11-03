# hil: HIL link frames (runner <-> controller over a serial line). Plain C, no OS. Built by any
# CMake project (add_subdirectory), as an ESP-IDF component, or by PlatformIO through library.json.
# hil_link.c and hil_port.c are the controller's end (hil_port.h); the platform supplies the line.
set(srcs src/hil.c src/hil_link.c src/hil_port.c)
if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(hil STATIC ${srcs})
target_include_directories(hil PUBLIC include)
