# The Reference FMUs the runner's tests use, into runner/fmus; the runner's configure step does
# the same when they are missing:
#   cmake [-DDEST=<dir>] -P cmake/fetch_reference_fmus.cmake
cmake_minimum_required(VERSION 3.18) # ARCHIVE_EXTRACT PATTERNS
include("${CMAKE_CURRENT_LIST_DIR}/versions.cmake")
if(NOT DEST)
  get_filename_component(DEST "${CMAKE_CURRENT_LIST_DIR}/../runner/fmus" ABSOLUTE)
endif()
krumpir_reference_fmus("${DEST}")
