# t12ctrl: the T12 controller (conversion, PI, heater cycle, safety, scenarios, the simulated
# front end), plain C. Needs the ctrlcore and t12 targets (krumpir_add_libs). An ESP-IDF component
# on the board; on the PC the static library t12ctrl (krumpir::t12ctrl).
set(srcs
  src/t12_convert.c
  src/t12_ctrl.c
  src/t12_cycle.c
  src/t12_frontend.c
  src/t12_io_sim.c
  src/t12_loop.c
  src/t12_safety.c
  src/t12_scenario.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES ctrlcore t12)
  return()
endif()

add_library(t12ctrl STATIC ${srcs})
add_library(krumpir::t12ctrl ALIAS t12ctrl)
target_include_directories(t12ctrl PUBLIC include)
target_link_libraries(t12ctrl PUBLIC ctrlcore t12 PRIVATE krumpir::warnings)
