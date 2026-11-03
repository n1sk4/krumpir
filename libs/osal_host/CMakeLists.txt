# Host (PC) port of the OSAL contract: the critical section and a table-driven console, for host
# tests. Provides the CMake target `osal_port` that the runtime links against.
add_library(osal_host STATIC src/console_host.c)
target_include_directories(osal_host PUBLIC include)
target_link_libraries(osal_host PUBLIC osal)
add_library(osal_port ALIAS osal_host)

# A controller on a PC on the HIL link (Linux): the platform (tick task, telemetry, HIL port) and
# the program's main. Linked by controllers/<x>/host; not built otherwise.
if(NOT WIN32)
  find_package(Threads REQUIRED)
  add_library(hil_host STATIC EXCLUDE_FROM_ALL src/hil_host.c src/main_host.c)
  target_link_libraries(hil_host PUBLIC osal_host hil rte Threads::Threads)
endif()
