# dcmotor: the DC motor plant model, plain C. An ESP-IDF component on the board (the on-chip
# simulation); on the PC the static library dcmotor (krumpir::dcmotor). The FMU is built by
# ../CMakeLists.txt.
set(srcs
  src/plant_sim.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(dcmotor STATIC ${srcs})
add_library(krumpir::dcmotor ALIAS dcmotor)
target_include_directories(dcmotor PUBLIC include)
target_link_libraries(dcmotor PUBLIC $<$<NOT:$<C_COMPILER_ID:MSVC>>:m> PRIVATE krumpir::warnings)
