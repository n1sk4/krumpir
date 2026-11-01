# ctrlcore: plain C, no framework. Built by any CMake project (add_subdirectory), as an ESP-IDF
# component, or by PlatformIO through library.json.
set(srcs src/loop_stats.c src/param_parse.c src/pid.c src/ring_buf.c src/step_metrics.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(ctrlcore STATIC ${srcs})
target_include_directories(ctrlcore PUBLIC include)
if(NOT MSVC)
  target_link_libraries(ctrlcore PUBLIC m)
endif()
