# T12 controller library: plain C, no framework. Built by any CMake project (needs the ctrlcore and
# t12 targets, see libs/cmake/krumpir.cmake), as an ESP-IDF component, or by PlatformIO through
# library.json.
set(srcs src/t12_convert.c src/t12_ctrl.c src/t12_cycle.c src/t12_frontend.c src/t12_io_sim.c
         src/t12_loop.c src/t12_safety.c src/t12_scenario.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES ctrlcore t12)
  return()
endif()

add_library(t12ctrl STATIC ${srcs})
target_include_directories(t12ctrl PUBLIC include)
target_link_libraries(t12ctrl PUBLIC ctrlcore t12)
