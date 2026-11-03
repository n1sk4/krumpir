# hil: the HIL link between the runner and a controller on a serial line. hil.c is the frames
# (both ends); hil_link.c and hil_port.c are the controller's end (hil_port.h), the platform
# supplies the line. Plain C, no OS. An ESP-IDF component on the board; on the PC the static
# library hil (krumpir::hil).
set(srcs
  src/hil.c
  src/hil_link.c
  src/hil_port.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(hil STATIC ${srcs})
add_library(krumpir::hil ALIAS hil)
target_include_directories(hil PUBLIC include)
target_link_libraries(hil PRIVATE krumpir::warnings)
