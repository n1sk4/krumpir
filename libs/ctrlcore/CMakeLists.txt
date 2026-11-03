# ctrlcore: PID, ring buffer, loop statistics, step-response figures, parameter parsing. Plain C.
# An ESP-IDF component on the board; on the PC the static library ctrlcore (krumpir::ctrlcore).
set(srcs
  src/loop_stats.c
  src/param_parse.c
  src/pid.c
  src/ring_buf.c
  src/step_metrics.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(ctrlcore STATIC ${srcs})
add_library(krumpir::ctrlcore ALIAS ctrlcore)
target_include_directories(ctrlcore PUBLIC include)
target_link_libraries(ctrlcore PUBLIC $<$<NOT:$<C_COMPILER_ID:MSVC>>:m> PRIVATE krumpir::warnings)
