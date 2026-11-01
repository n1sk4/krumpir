# dcmotor plant: plain C, no framework. Built by any CMake project (add_subdirectory), as an
# ESP-IDF component, or by PlatformIO through library.json. The FMU is built by ../CMakeLists.txt.
set(srcs src/plant_sim.c)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
  return()
endif()

add_library(dcmotor STATIC ${srcs})
target_include_directories(dcmotor PUBLIC include)
if(NOT MSVC)
  target_link_libraries(dcmotor PUBLIC m)
endif()
