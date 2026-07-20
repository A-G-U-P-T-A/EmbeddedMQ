cmake_minimum_required(VERSION 3.16)

option(EMQ_BUILD_TRANSPORT_TESTS "Build transport unit tests" ON)

if(NOT EMQ_CORE_DIR)
  set(EMQ_CORE_DIR "${CMAKE_SOURCE_DIR}/core")
endif()

list(APPEND CMAKE_MODULE_PATH "${EMQ_CORE_DIR}/cmake")

add_subdirectory(ipc)
add_subdirectory(net)
