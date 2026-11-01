# OSAL contract plus its platform-independent parts (src/). A platform port (libs/esp/espglue for
# ESP-IDF, libs/osal_host for the PC, later Zephyr) provides osal_port.h and implements the service
# headers; in CMake it is the target `osal_port`, which links to this one.
set(srcs src/console_args.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES ctrlcore)
  return()
endif()

add_library(osal STATIC ${srcs})
target_include_directories(osal PUBLIC include)
target_link_libraries(osal PUBLIC ctrlcore)
