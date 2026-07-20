add_library(emq_diff_model STATIC diff_model.c)
target_include_directories(emq_diff_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${EMQ_CORE_DIR}/include)
emq_apply_warnings(emq_diff_model)

emq_add_suite(diff_runner diff_runner.c difftest)
target_link_libraries(diff_runner PRIVATE emq_diff_model)
target_include_directories(diff_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(diff_runner PROPERTIES TIMEOUT 120)
