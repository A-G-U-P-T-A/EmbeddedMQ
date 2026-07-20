cmake_minimum_required(VERSION 3.16)
project(EmbeddedMQ_Monorepo VERSION 0.1.0 LANGUAGES C)

option(EMQ_BUILD_TRANSPORT "Build transport plane (ipc + net)" OFF)

# Thin monorepo wrapper. Prefer building the engine with:
#   cmake -S core -B build
# or from the repo root via this file:
#   cmake -S . -B build
add_subdirectory(core)

if(EMQ_BUILD_TRANSPORT)
  add_subdirectory(transport)
endif()
