cmake_minimum_required(VERSION 3.16)

add_library(emq_ipc STATIC src/emq_ipc.c)
target_include_directories(emq_ipc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/transport/ipc>
  PRIVATE
    ${EMQ_CORE_DIR}/include)

if(NOT TARGET emq_apply_warnings)
  include(compiler-warnings)
endif()
emq_apply_warnings(emq_ipc)

if(WIN32)
  target_compile_definitions(emq_ipc PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(emq_ipc PRIVATE rt)
endif()

if(EMQ_BUILD_TRANSPORT_TESTS)
  enable_testing()
  add_executable(test_ipc tests/test_ipc.c)
  target_link_libraries(test_ipc PRIVATE emq_ipc)
  target_include_directories(test_ipc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EMQ_CORE_DIR}/include)
  emq_apply_warnings(test_ipc)
  add_test(NAME test_ipc COMMAND test_ipc)
endif()
