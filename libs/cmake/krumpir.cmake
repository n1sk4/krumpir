# Host builds of the portable libraries and their Unity tests, without PlatformIO.
#
#   include(<repo>/libs/cmake/krumpir.cmake)
#   krumpir_add_libs()                         # targets ctrlcore, dcmotor, t12, unity
#   krumpir_unity_tests(<dir> <libraries...>)  # each <dir>/test_*/test_main.c -> one CTest test
#
# Each library's own CMakeLists.txt also works as an ESP-IDF component, and PlatformIO keeps
# building them from library.json.
get_filename_component(KRUMPIR_LIBS_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

macro(krumpir_add_libs)
  foreach(lib ctrlcore plants/dcmotor plants/t12)
    get_filename_component(_name ${lib} NAME)
    if(NOT TARGET ${_name})
      add_subdirectory("${KRUMPIR_LIBS_DIR}/${lib}" "${CMAKE_BINARY_DIR}/krumpir_libs/${_name}")
    endif()
  endforeach()
  if(NOT TARGET unity)
    add_library(unity STATIC "${KRUMPIR_LIBS_DIR}/vendor/unity/unity.c")
    target_include_directories(unity PUBLIC "${KRUMPIR_LIBS_DIR}/vendor/unity")
  endif()
endmacro()

function(krumpir_unity_tests dir)
  file(GLOB tests LIST_DIRECTORIES true "${dir}/test_*")
  foreach(t ${tests})
    if(EXISTS "${t}/test_main.c")
      get_filename_component(name "${t}" NAME)
      add_executable(${name} "${t}/test_main.c")
      target_include_directories(${name} PRIVATE "${t}") # test data headers next to the test
      target_link_libraries(${name} PRIVATE unity ${ARGN})
      add_test(NAME ${name} COMMAND ${name})
    endif()
  endforeach()
endfunction()
