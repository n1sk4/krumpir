# Host (PC) port of the OSAL contract: the critical section and a table-driven console, for host
# tests. Provides the CMake target `osal_port` that the runtime links against.
add_library(osal_host STATIC src/console_host.c)
target_include_directories(osal_host PUBLIC include)
target_link_libraries(osal_host PUBLIC osal)
add_library(osal_port ALIAS osal_host)
