# rte: the controller runtime: parameter table with limits, run/stop/clear and trip handling,
# hand-over to the tick, calibration page mapping, console commands. Plain C on the OSAL contract:
# on the PC it links the platform port through osal_port (libs/osal_host); on the board the port
# comes with the firmware. An ESP-IDF component on the board; on the PC the static library rte
# (krumpir::rte).
set(srcs
  src/rte.c
  src/rte_calib.c
  src/rte_cmd.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES osal)
  return()
endif()

add_library(rte STATIC ${srcs})
add_library(krumpir::rte ALIAS rte)
target_include_directories(rte PUBLIC include)
target_link_libraries(rte PUBLIC osal osal_port PRIVATE krumpir::warnings)
