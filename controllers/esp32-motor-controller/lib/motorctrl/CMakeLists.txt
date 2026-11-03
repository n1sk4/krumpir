# motorctrl: the motor controller (PI, safety, the loop, scenarios, the simulated motor), plain C.
# Needs the ctrlcore and dcmotor targets (krumpir_add_libs). An ESP-IDF component on the board; on
# the PC the static library motorctrl (krumpir::motorctrl).
set(srcs
  src/motor_ctrl.c
  src/motor_io_sim.c
  src/motor_loop.c
  src/motor_safety.c
  src/motor_scenario.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES ctrlcore dcmotor)
  return()
endif()

add_library(motorctrl STATIC ${srcs})
add_library(krumpir::motorctrl ALIAS motorctrl)
target_include_directories(motorctrl PUBLIC include)
target_link_libraries(motorctrl PUBLIC ctrlcore dcmotor PRIVATE krumpir::warnings)
