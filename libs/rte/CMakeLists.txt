# Controller runtime: plain C on the OSAL contract. In a CMake build it links the platform port
# through the target `osal_port` (libs/osal_host on the PC); as an ESP-IDF component the port comes
# with the firmware. PlatformIO builds it from library.json.
set(srcs src/rte.c src/rte_calib.c src/rte_cmd.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES osal)
  return()
endif()

add_library(rte STATIC ${srcs})
target_include_directories(rte PUBLIC include)
target_link_libraries(rte PUBLIC osal osal_port)
