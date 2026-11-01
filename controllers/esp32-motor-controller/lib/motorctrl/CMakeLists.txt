# Motor controller library: plain C, no framework. Built by any CMake project (needs the ctrlcore
# and dcmotor targets, see libs/cmake/krumpir.cmake), as an ESP-IDF component, or by PlatformIO
# through library.json.
set(srcs src/motor_ctrl.c src/motor_io_sim.c src/motor_loop.c src/motor_safety.c
         src/motor_scenario.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES ctrlcore dcmotor)
  return()
endif()

add_library(motorctrl STATIC ${srcs})
target_include_directories(motorctrl PUBLIC include)
target_link_libraries(motorctrl PUBLIC ctrlcore dcmotor)
