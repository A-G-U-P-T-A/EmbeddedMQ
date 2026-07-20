add_executable(emq_bench bench_latency.c)
target_link_libraries(emq_bench PRIVATE emq)
target_include_directories(emq_bench PRIVATE ${EMQ_CORE_DIR}/src)
emq_apply_warnings(emq_bench)

add_executable(emq_bench_load bench_load.c bench_metrics.c)
target_link_libraries(emq_bench_load PRIVATE emq)
target_include_directories(emq_bench_load PRIVATE ${EMQ_CORE_DIR}/src
                                                   ${CMAKE_CURRENT_SOURCE_DIR})
emq_apply_warnings(emq_bench_load)
if(WIN32)
  target_compile_definitions(emq_bench_load PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq_bench_load PRIVATE psapi)
endif()

add_executable(emq_bench_compare compare/bench_compare.c bench_metrics.c)
target_link_libraries(emq_bench_compare PRIVATE emq)
target_include_directories(emq_bench_compare PRIVATE
  ${EMQ_CORE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR})
emq_apply_warnings(emq_bench_compare)
if(WIN32)
  target_compile_definitions(emq_bench_compare PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq_bench_compare PRIVATE psapi)
endif()

add_executable(emq_bench_fanout bench_fanout.c bench_metrics.c)
target_link_libraries(emq_bench_fanout PRIVATE emq)
target_include_directories(emq_bench_fanout PRIVATE
  ${EMQ_CORE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
emq_apply_warnings(emq_bench_fanout)
if(WIN32)
  target_compile_definitions(emq_bench_fanout PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq_bench_fanout PRIVATE psapi)
endif()

add_executable(emq_bench_sched bench_sched.c bench_metrics.c)
target_link_libraries(emq_bench_sched PRIVATE emq)
target_include_directories(emq_bench_sched PRIVATE
  ${EMQ_CORE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
emq_apply_warnings(emq_bench_sched)
if(WIN32)
  target_compile_definitions(emq_bench_sched PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq_bench_sched PRIVATE psapi)
endif()
