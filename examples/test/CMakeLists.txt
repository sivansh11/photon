cmake_minimum_required(VERSION 3.15)

project(test)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/OUTPUT/${PROJECT_NAME}")

file(GLOB_RECURSE CPP_SRC_FILES ./*.cpp)

add_executable(test ${CPP_SRC_FILES})

if(WIN32)
    add_custom_command(
        TARGET test
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:test>
                $<TARGET_FILE_DIR:test>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying required DLLs to output directory"
    )
endif()

target_link_libraries(test
	PUBLIC horizon
  PUBLIC photon
)

target_include_directories(test
	PUBLIC horizon
)
