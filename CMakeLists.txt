cmake_minimum_required(VERSION 3.22)

project(photon)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/OUTPUT/")
set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

FetchContent_Declare(
  horizon
  GIT_REPOSITORY https://github.com/sivansh11/horizon
  GIT_TAG main
)

if (NOT horizon_POPULATED)
  FetchContent_MakeAvailable(horizon)
endif()

file(GLOB_RECURSE SRC_FILES src/*.cpp)

add_library(photon
  ${SRC_FILES}
)

target_include_directories(photon
  PUBLIC include/
)

target_link_libraries(photon
  PUBLIC horizon
)

if (PHOTON_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

