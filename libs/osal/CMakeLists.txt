# OSAL contract: headers only, no platform code. A platform port (libs/esp/espglue for ESP-IDF,
# later Zephyr or a host port) provides osal_port.h and implements the service headers.
if(ESP_PLATFORM)
  idf_component_register(INCLUDE_DIRS include REQUIRES ctrlcore)
  return()
endif()

add_library(osal INTERFACE)
target_include_directories(osal INTERFACE include)
target_link_libraries(osal INTERFACE ctrlcore)
