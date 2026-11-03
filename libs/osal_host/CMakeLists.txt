# osal_host: the PC port of the OSAL contract: the critical section and a table-driven console,
# for host tests (osal_host, also osal_port and krumpir::osal_host). PC only, no ESP-IDF branch.
#
# hil_host: a controller on a PC on the HIL link (Linux): the platform (tick task, telemetry, HIL
# port) and the program's main. Linked by controllers/<x>/host only, so not built otherwise.
set(srcs
  src/console_host.c)

add_library(osal_host STATIC ${srcs})
add_library(osal_port ALIAS osal_host)
add_library(krumpir::osal_host ALIAS osal_host)
target_include_directories(osal_host PUBLIC include)
target_link_libraries(osal_host PUBLIC osal PRIVATE krumpir::warnings)

if(NOT WIN32)
  set(hil_host_srcs
    src/hil_host.c
    src/main_host.c)
  find_package(Threads REQUIRED)
  add_library(hil_host STATIC EXCLUDE_FROM_ALL ${hil_host_srcs})
  add_library(krumpir::hil_host ALIAS hil_host)
  target_link_libraries(hil_host PUBLIC osal_host hil rte Threads::Threads PRIVATE krumpir::warnings)
endif()
