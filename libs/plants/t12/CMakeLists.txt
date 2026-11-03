# t12: the T12 soldering tip plant model, plain C. An ESP-IDF component on the board (the on-chip
# simulation); on the PC the static library t12 (krumpir::t12). The FMU is built by
# ../CMakeLists.txt.
set(srcs
  src/t12_plant.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(t12 STATIC ${srcs})
add_library(krumpir::t12 ALIAS t12)
target_include_directories(t12 PUBLIC include)
target_link_libraries(t12 PUBLIC $<$<NOT:$<C_COMPILER_ID:MSVC>>:m> PRIVATE krumpir::warnings)
