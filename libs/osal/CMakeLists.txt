# osal: the OS abstraction contract (critical section, tick task, telemetry, console) and its
# platform-independent parts. A platform port (libs/esp/espglue on the board, libs/osal_host on the
# PC) provides osal_port.h and implements the rest; in CMake it is the target osal_port. An ESP-IDF
# component on the board; on the PC the static library osal (krumpir::osal).
set(srcs
  src/console_args.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES ctrlcore)
  return()
endif()

add_library(osal STATIC ${srcs})
add_library(krumpir::osal ALIAS osal)
target_include_directories(osal PUBLIC include)
target_link_libraries(osal PUBLIC ctrlcore PRIVATE krumpir::warnings)
