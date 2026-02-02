cmake_minimum_required(VERSION 3.16)
project(papyrix-tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Get project root (parent of test directory)
get_filename_component(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

# MSVC: Enable UTF-8 source and execution character set
if(MSVC)
  add_compile_options(/utf-8)
endif()

add_compile_definitions(TEST_BUILD)

# Include paths for mocks and test helpers
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Include paths for source code under test
include_directories(
  ${PROJECT_ROOT}/src
  ${PROJECT_ROOT}/src/core
  ${PROJECT_ROOT}/src/content
  ${PROJECT_ROOT}/lib/Utf8
  ${PROJECT_ROOT}/lib/Serialization
  ${PROJECT_ROOT}/lib/AsyncTask/src
  ${PROJECT_ROOT}/lib/Epub/Epub
  ${PROJECT_ROOT}/lib/Epub/Epub/css
  ${PROJECT_ROOT}/lib/PageCache
  ${PROJECT_ROOT}/lib/FsHelpers
  ${PROJECT_ROOT}/lib/ScriptDetector
  ${PROJECT_ROOT}/lib/Markdown
  ${PROJECT_ROOT}/lib/Txt
  ${PROJECT_ROOT}/lib/GfxRenderer
)

# Common test helpers
set(TEST_HELPERS
  ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/platform_stubs.cpp
)

# Auto-discover and build tests
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)

foreach(TEST_SRC ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

  # Special handling for tests that need additional source files
  if(TEST_NAME STREQUAL "MarkdownParserTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/md_parser.c
      ${TEST_HELPERS}
    )
  else()
    add_executable(${TEST_NAME} ${TEST_SRC} ${TEST_HELPERS})
  endif()

  target_include_directories(${TEST_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  set_target_properties(${TEST_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
endforeach()

message(STATUS "Configured ${TEST_SOURCES} tests")
