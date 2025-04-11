cmake_minimum_required(VERSION 3.22)

project(vision)

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

add_executable(vision
  ${SRC_FILES}
)

target_link_libraries(vision
  PUBLIC horizon
)

