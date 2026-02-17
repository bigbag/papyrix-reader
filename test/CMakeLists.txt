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
  ${PROJECT_ROOT}/lib/EpdFont
  ${PROJECT_ROOT}/lib/ArabicShaper
)

# Common test helpers
set(TEST_HELPERS
  ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/test_font_data.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/platform_stubs.cpp
)

# Auto-discover and build tests
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)

foreach(TEST_SRC ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

  # Special handling for tests that need additional source files
  if(TEST_NAME STREQUAL "ArabicShaperTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ArabicShaper/ArabicCharacter.cpp
      ${PROJECT_ROOT}/lib/ArabicShaper/ArabicShaper.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ScriptDetectorTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ScriptDetector/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "MarkdownParserTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/md_parser.c
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "CssParserClassTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/Epub/css/CssParser.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "XtcCoverHelperTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Xtc/Xtc/XtcParser.cpp
      ${PROJECT_ROOT}/lib/Xtc/XtcCoverHelper.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Xtc
      ${PROJECT_ROOT}/lib/Xtc/Xtc
    )
  elseif(TEST_NAME STREQUAL "HtmlEntitiesTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/Epub/htmlEntities.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ChapterHtmlSlimParserTest")
    # Find expat library - test uses expat directly for XML parsing
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/Epub/htmlEntities.cpp
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0 XML_DTD)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
  elseif(TEST_NAME STREQUAL "ContentOpfParserTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Utf8/Utf8Nfc.cpp
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
  elseif(TEST_NAME STREQUAL "Fb2MetadataTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Fb2
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
