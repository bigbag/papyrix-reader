cmake_minimum_required(VERSION 3.16)
project(reader-test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_compile_definitions(TEST_BUILD XML_GE=0 XML_DTD _LARGEFILE64_SOURCE)

find_package(EXPAT REQUIRED)

add_executable(reader-test
  main.cpp
  mocks/platform_stubs.cpp

  # Content handlers
  ${PROJECT_ROOT}/lib/Txt/Txt.cpp
  ${PROJECT_ROOT}/lib/Markdown/Markdown.cpp
  ${PROJECT_ROOT}/lib/Fb2/Fb2.cpp
  ${PROJECT_ROOT}/lib/Fb2/Fb2Parser.cpp

  # Text layout
  ${PROJECT_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/Page.cpp

  # TXT + Markdown parsers
  ${PROJECT_ROOT}/lib/PageCache/PlainTextParser.cpp
  ${PROJECT_ROOT}/lib/Markdown/MarkdownParser.cpp
  ${PROJECT_ROOT}/lib/Markdown/md_parser.c

  # EPUB chain
  ${PROJECT_ROOT}/lib/Epub/Epub.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/parsers/ContainerParser.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/parsers/TocNcxParser.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/parsers/TocNavParser.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${PROJECT_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${PROJECT_ROOT}/lib/PageCache/EpubChapterParser.cpp
  ${PROJECT_ROOT}/lib/PageCache/PageCache.cpp

  # Font support
  ${PROJECT_ROOT}/lib/EpdFont/EpdFont.cpp
  ${PROJECT_ROOT}/lib/EpdFont/EpdFontFamily.cpp

  # Support
  ${PROJECT_ROOT}/lib/Html5/Html5Normalizer.cpp
  ${PROJECT_ROOT}/lib/ZipFile/ZipFile.cpp
  ${PROJECT_ROOT}/lib/miniz/miniz.c
  ${PROJECT_ROOT}/lib/ScriptDetector/ScriptDetector.cpp
  ${PROJECT_ROOT}/lib/Utf8/Utf8.cpp
  ${PROJECT_ROOT}/lib/Utf8/Utf8Nfc.cpp
  ${PROJECT_ROOT}/lib/FsHelpers/FsHelpers.cpp
)

# Mock headers take priority over real ones
target_include_directories(reader-test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks
  ${PROJECT_ROOT}/src
  ${PROJECT_ROOT}/src/core
  ${PROJECT_ROOT}/src/content
  ${PROJECT_ROOT}/lib/Utf8
  ${PROJECT_ROOT}/lib/Serialization
  ${PROJECT_ROOT}/lib/AsyncTask/src
  ${PROJECT_ROOT}/lib/Epub
  ${PROJECT_ROOT}/lib/Epub/Epub
  ${PROJECT_ROOT}/lib/Epub/Epub/css
  ${PROJECT_ROOT}/lib/PageCache
  ${PROJECT_ROOT}/lib/FsHelpers
  ${PROJECT_ROOT}/lib/ScriptDetector
  ${PROJECT_ROOT}/lib/Markdown
  ${PROJECT_ROOT}/lib/Txt
  ${PROJECT_ROOT}/lib/Fb2
  ${PROJECT_ROOT}/lib/GfxRenderer
  ${PROJECT_ROOT}/lib/EpdFont
  ${PROJECT_ROOT}/lib/EpdFont/builtinFonts
  ${PROJECT_ROOT}/lib/ArabicShaper
  ${PROJECT_ROOT}/lib/ThaiShaper
  ${PROJECT_ROOT}/lib/ZipFile
  ${PROJECT_ROOT}/lib/Html5
  ${PROJECT_ROOT}/lib/miniz
  ${PROJECT_ROOT}/lib/ImageConverter
)

# Force-include Arduino.h like PlatformIO does (provides cstdint, Print, etc.)
target_compile_options(reader-test PRIVATE -include Arduino.h)

target_link_libraries(reader-test PRIVATE EXPAT::EXPAT)
