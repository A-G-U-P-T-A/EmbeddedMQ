cmake_minimum_required(VERSION 3.16)
project(EmbeddedMQ VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EMQ_BUILD_TESTS "Build unit tests" ON)
option(EMQ_BUILD_BENCH "Build benchmarks" ON)
option(EMQ_BUILD_EXAMPLES "Build example programs" ON)
option(EMQ_BUILD_STRESS "Build stress/fuzz/fault/recovery/soak suites" ON)
option(EMQ_FAULT_INJECT "Enable fault-injection hooks (zero overhead when OFF)" OFF)
option(EMQ_ENABLE_SANITIZERS "Enable Address/UB sanitizers (non-MSVC, legacy)" OFF)
option(EMQ_ASAN "Enable MSVC AddressSanitizer" OFF)
set(EMQ_SANITIZE "" CACHE STRING "Clang/GCC sanitizers: address;undefined;thread")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(compiler-warnings)
include(sanitizers)

option(EMQ_WITH_LZ4 "Enable optional LZ4 compression hooks" OFF)
option(EMQ_WITH_ZSTD "Enable optional ZSTD compression hooks" OFF)

set(EMQ_SOURCES
  src/platform/emq_platform.c
  src/core/emq_alloc.c
  src/core/emq_crc.c
  src/core/emq_record.c
  src/core/emq_log.c
  src/core/emq_pool.c
  src/core/emq_bitmap.c
  src/core/emq_ebr.c
  src/core/emq_lfq.c
  src/core/emq_cpu.c
  src/core/emq_domain.c
  src/core/emq_hist.c
  src/core/emq_stats_ex.c
  src/core/emq_task.c
  src/core/emq_compress.c
  src/core/emq_fault.c
  src/policy/emq_policy.c
  src/registry/emq_registry.c
  src/sched/emq_wheel.c
  src/sched/emq_sched.c
  src/routing/emq_routing.c
  src/primitives/emq_fifo.c
  src/primitives/emq_priority.c
  src/primitives/emq_ring.c
  src/primitives/emq_delay.c
  src/primitives/emq_workqueue.c
  src/primitives/emq_stream.c
  src/primitives/emq_pubsub.c
  src/primitives/emq_dispatch.c
  src/engine/emq_mpmc.c
  src/engine/emq_engine.c
  src/engine/emq_eventloop.c
  src/api/emq.c
)

add_library(emq STATIC ${EMQ_SOURCES})

target_include_directories(emq
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(EMQ_FAULT_INJECT)
  target_compile_definitions(emq PUBLIC EMQ_FAULT_INJECT=1)
endif()

emq_apply_warnings(emq)
emq_apply_sanitizers(emq)

if(WIN32)
  target_compile_definitions(emq PRIVATE EMQ_PLATFORM_WINDOWS _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(emq PRIVATE Synchronization)
else()
  target_compile_definitions(emq PRIVATE EMQ_PLATFORM_POSIX)
  # Strict -std=c11 hides POSIX clocks/pread/futex without feature macros.
  if(APPLE)
    target_compile_definitions(emq PRIVATE _DARWIN_C_SOURCE)
  else()
    target_compile_definitions(emq PRIVATE _GNU_SOURCE)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(emq PRIVATE Threads::Threads)
endif()

if(EMQ_BUILD_TESTS OR EMQ_BUILD_STRESS)
  enable_testing()
endif()

if(EMQ_BUILD_TESTS OR EMQ_BUILD_STRESS OR EMQ_BUILD_BENCH)
  add_subdirectory(testsupport)
endif()

if(EMQ_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(EMQ_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(EMQ_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(EMQ_BUILD_STRESS)
  add_subdirectory(stress)
  add_subdirectory(fuzz)
  add_subdirectory(fault)
  add_subdirectory(recovery)
  add_subdirectory(soak)
  add_subdirectory(difftest)
  add_subdirectory(model)
endif()

install(TARGETS emq ARCHIVE DESTINATION lib)
install(DIRECTORY include/emq DESTINATION include)
