emq_add_suite(recovery_supervisor recovery_supervisor.c recovery)

add_test(NAME recovery_crashpoint COMMAND recovery_supervisor --quick --crashpoint)
set_tests_properties(recovery_crashpoint PROPERTIES LABELS "recovery" TIMEOUT 120)
if(NOT EMQ_FAULT_INJECT)
  set_tests_properties(recovery_crashpoint PROPERTIES DISABLED TRUE)
endif()

add_executable(recovery_writer recovery_writer.c)
target_link_libraries(recovery_writer PRIVATE emq emq_testsupport)
target_include_directories(recovery_writer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/testsupport)
emq_apply_warnings(recovery_writer)
if(WIN32)
  target_compile_definitions(recovery_writer PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
