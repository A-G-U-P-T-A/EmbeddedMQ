cmake_minimum_required(VERSION 3.16)

add_library(emq_net STATIC
  src/emq_net_socket.c
  src/emq_net_driver.c)
target_include_directories(emq_net
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/transport/net>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${EMQ_CORE_DIR}/include)

if(NOT TARGET emq_apply_warnings)
  include(compiler-warnings)
endif()
emq_apply_warnings(emq_net)

if(WIN32)
  target_compile_definitions(emq_net PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq_net PRIVATE ws2_32)
else()
  target_link_libraries(emq_net PRIVATE m)
endif()

if(EMQ_BUILD_TRANSPORT_TESTS)
  enable_testing()
  add_executable(test_net_loopback tests/test_net_loopback.c)
  target_link_libraries(test_net_loopback PRIVATE emq_net)
  target_include_directories(test_net_loopback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${EMQ_CORE_DIR}/include)
  emq_apply_warnings(test_net_loopback)
  add_test(NAME test_net_loopback COMMAND test_net_loopback)
endif()
