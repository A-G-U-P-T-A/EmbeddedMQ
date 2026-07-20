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

# Absolute root of the C engine tree (works when included via add_subdirectory).
set(EMQ_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

list(APPEND CMAKE_MODULE_PATH "${EMQ_CORE_DIR}/cmake")
include(compiler-warnings)
include(sanitizers)

option(EMQ_WITH_LZ4 "Enable optional LZ4 compression hooks" OFF)
option(EMQ_WITH_ZSTD "Enable optional ZSTD compression hooks" OFF)
option(EMQ_BUILD_SHARED "Build shared libemq (in addition to static)" OFF)
option(EMQ_ENABLE_COVERAGE "Enable gcov/llvm-cov instrumentation" OFF)

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
add_library(EmbeddedMQ::emq ALIAS emq)

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

if(EMQ_ENABLE_COVERAGE AND NOT MSVC)
  target_compile_options(emq PUBLIC --coverage -fprofile-arcs -ftest-coverage)
  target_link_options(emq PUBLIC --coverage)
  target_compile_definitions(emq PUBLIC EMQ_COVERAGE_BUILD=1)
endif()

emq_apply_warnings(emq)
emq_apply_sanitizers(emq)

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(emq PRIVATE EMQ_PLATFORM_WINDOWS)
  target_link_libraries(emq PRIVATE Synchronization)
else()
  # Strict -std=c11 hides POSIX clocks/pread/setenv/truncate without these.
  if(APPLE)
    add_compile_definitions(_DARWIN_C_SOURCE)
  else()
    add_compile_definitions(_GNU_SOURCE)
  endif()
  target_compile_definitions(emq PRIVATE EMQ_PLATFORM_POSIX)
  find_package(Threads REQUIRED)
  target_link_libraries(emq PRIVATE Threads::Threads)
endif()

if(EMQ_BUILD_SHARED)
  add_library(emq_shared SHARED ${EMQ_SOURCES})
  set_target_properties(emq_shared PROPERTIES
    OUTPUT_NAME emq
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  target_include_directories(emq_shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(emq_shared PUBLIC EMQ_BUILD_SHARED=1
                                       PRIVATE EMQ_EXPORTS=1)
  if(EMQ_FAULT_INJECT)
    target_compile_definitions(emq_shared PUBLIC EMQ_FAULT_INJECT=1)
  endif()
  emq_apply_warnings(emq_shared)
  emq_apply_sanitizers(emq_shared)
  if(WIN32)
    target_compile_definitions(emq_shared PRIVATE EMQ_PLATFORM_WINDOWS
                                                  _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(emq_shared PRIVATE Synchronization)
    set_target_properties(emq_shared PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    target_sources(emq_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emq.def)
  else()
    if(APPLE)
      target_compile_definitions(emq_shared PRIVATE _DARWIN_C_SOURCE)
    else()
      target_compile_definitions(emq_shared PRIVATE _GNU_SOURCE)
      set_target_properties(emq_shared PROPERTIES
        LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/emq.map")
    endif()
    target_compile_definitions(emq_shared PRIVATE EMQ_PLATFORM_POSIX)
    target_link_libraries(emq_shared PRIVATE Threads::Threads)
  endif()
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

include(CMakePackageConfigHelpers)
install(TARGETS emq
  EXPORT EmbeddedMQTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
if(EMQ_BUILD_SHARED)
  install(TARGETS emq_shared
    EXPORT EmbeddedMQTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
endif()
install(DIRECTORY include/emq DESTINATION include)
install(EXPORT EmbeddedMQTargets
  FILE EmbeddedMQTargets.cmake
  NAMESPACE EmbeddedMQ::
  DESTINATION lib/cmake/EmbeddedMQ)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/emq.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/emq.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/emq.pc
        DESTINATION lib/pkgconfig)
