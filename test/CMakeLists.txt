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
  ${PROJECT_ROOT}/lib/Utf8/src
  ${PROJECT_ROOT}/lib/Serialization/src
  ${PROJECT_ROOT}/lib/AsyncTask/src
  ${PROJECT_ROOT}/lib/RenderTypes/src
  ${PROJECT_ROOT}/lib/Epub/src/Epub
  ${PROJECT_ROOT}/lib/Epub/src/Epub/css
  ${PROJECT_ROOT}/lib/PageCache/src
  ${PROJECT_ROOT}/lib/FsHelpers/src
  ${PROJECT_ROOT}/lib/ScriptDetector/src
  ${PROJECT_ROOT}/lib/Markdown/src
  ${PROJECT_ROOT}/lib/Txt/src
  ${PROJECT_ROOT}/lib/GfxRenderer/src
  ${PROJECT_ROOT}/lib/EpdFont/src
  ${PROJECT_ROOT}/lib/ArabicShaper/src
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
  if(TEST_NAME STREQUAL "ThaiShaperTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiCharacter.cpp
      ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiClusterBuilder.cpp
      ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiWordBreak.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
  elseif(TEST_NAME STREQUAL "ArabicShaperTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ArabicShaper/src/ArabicCharacter.cpp
      ${PROJECT_ROOT}/lib/ArabicShaper/src/ArabicShaper.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "FsHelpersNormaliseTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME MATCHES "^ScriptDetector.*Test$")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME MATCHES "^MarkdownParser.*Test$")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "CssParserClassTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/src/Epub/css/CssParser.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "XtcCoverHelperTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc/XtcParser.cpp
      ${PROJECT_ROOT}/lib/Xtc/src/XtcCoverHelper.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Xtc/src
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc
    )
  elseif(TEST_NAME STREQUAL "HyphenationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Hyphenation/src
    )
  elseif(TEST_NAME STREQUAL "HtmlEntitiesTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/src/Epub/htmlEntities.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ChapterHtmlSlimParserTest")
    # Find expat library - test uses expat directly for XML parsing
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/src/Epub/htmlEntities.cpp
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0 XML_DTD)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
  elseif(TEST_NAME STREQUAL "ContentOpfParserTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
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
      ${PROJECT_ROOT}/lib/Fb2/src
    )
  elseif(TEST_NAME STREQUAL "LoggingTest")
    # Self-contained test with its own HardwareSerial.h mock (no TEST_HELPERS —
    # platform_stubs.h conflicts with the real Logging.h's logSerial/LOG_* defs).
    # BEFORE ensures our local mock and real Logging.h are found before test/mocks/.
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Logging/src/Logging.cpp
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/logging   # Local HWCDC mock (before test/mocks/)
      ${PROJECT_ROOT}/lib/Logging/src             # Real Logging.h
    )
    target_compile_definitions(${TEST_NAME} PRIVATE ENABLE_SERIAL_LOG LOG_LEVEL=2)
  elseif(TEST_NAME STREQUAL "InflateReaderTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/InflateReader/src/InflateReader.cpp
      ${PROJECT_ROOT}/lib/uzlib/src/tinflate.c
      ${PROJECT_ROOT}/lib/uzlib/src/adler32.c
      ${PROJECT_ROOT}/lib/uzlib/src/crc32.c
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/InflateReader/src
      ${PROJECT_ROOT}/lib/uzlib/src
    )
  elseif(TEST_NAME STREQUAL "test_BookmarkManager")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
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
