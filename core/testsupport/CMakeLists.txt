add_library(emq_testsupport STATIC emq_testsupport.c)
target_include_directories(emq_testsupport PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${EMQ_CORE_DIR}/src)
target_link_libraries(emq_testsupport PUBLIC emq)
emq_apply_warnings(emq_testsupport)
if(WIN32)
  target_compile_definitions(emq_testsupport PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq_testsupport PRIVATE psapi)
endif()

function(emq_add_suite name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE emq emq_testsupport)
  target_include_directories(${name} PRIVATE
    ${EMQ_CORE_DIR}/src
    ${EMQ_CORE_DIR}/include
    ${EMQ_CORE_DIR}/testsupport)
  emq_apply_warnings(${name})
  if(WIN32)
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  if(ARGC GREATER 2)
    set(_label ${ARGV2})
    add_test(NAME ${name} COMMAND ${name} --quick)
    set_tests_properties(${name} PROPERTIES LABELS "${_label}" TIMEOUT 120)
  endif()
endfunction()
