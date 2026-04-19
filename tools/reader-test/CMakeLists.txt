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
  ${PROJECT_ROOT}/lib/Txt/src/Txt.cpp
  ${PROJECT_ROOT}/lib/Markdown/src/Markdown.cpp
  ${PROJECT_ROOT}/lib/Fb2/src/Fb2.cpp
  ${PROJECT_ROOT}/lib/Fb2/src/Fb2Parser.cpp
  ${PROJECT_ROOT}/lib/Html/src/Html.cpp
  ${PROJECT_ROOT}/lib/PageCache/src/HtmlParser.cpp

  # Text layout
  ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
  ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
  ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/ImageBlock.cpp
  ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp

  # TXT + Markdown parsers
  ${PROJECT_ROOT}/lib/PageCache/src/PlainTextParser.cpp
  ${PROJECT_ROOT}/lib/Markdown/src/MarkdownParser.cpp
  ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c

  # EPUB chain
  ${PROJECT_ROOT}/lib/Epub/src/Epub.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/BookMetadataCache.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/ContainerParser.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/ContentOpfParser.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/TocNcxParser.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/TocNavParser.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/htmlEntities.cpp
  ${PROJECT_ROOT}/lib/Epub/src/Epub/css/CssParser.cpp
  ${PROJECT_ROOT}/lib/PageCache/src/EpubChapterParser.cpp
  ${PROJECT_ROOT}/lib/PageCache/src/PageCache.cpp

  # Font support
  ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
  ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
  ${PROJECT_ROOT}/lib/ExternalFont/src/ExternalFont.cpp

  # Hyphenation
  ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
  ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
  ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
  ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
  ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp

  # Support
  ${PROJECT_ROOT}/lib/Html5/src/Html5Normalizer.cpp
  ${PROJECT_ROOT}/lib/ZipFile/src/ZipFile.cpp
  ${PROJECT_ROOT}/lib/InflateReader/src/InflateReader.cpp
  ${PROJECT_ROOT}/lib/uzlib/src/tinflate.c
  ${PROJECT_ROOT}/lib/uzlib/src/adler32.c
  ${PROJECT_ROOT}/lib/uzlib/src/crc32.c
  ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
  ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
  ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
  ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
)

# Mock headers take priority over real ones
target_include_directories(reader-test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks
  ${PROJECT_ROOT}/src
  ${PROJECT_ROOT}/src/core
  ${PROJECT_ROOT}/src/content
  ${PROJECT_ROOT}/lib/Utf8/src
  ${PROJECT_ROOT}/lib/Encoding/src
  ${PROJECT_ROOT}/lib/Serialization/src
  ${PROJECT_ROOT}/lib/AsyncTask/src
  ${PROJECT_ROOT}/lib/RenderTypes/src
  ${PROJECT_ROOT}/lib/Epub/src
  ${PROJECT_ROOT}/lib/Epub/src/Epub
  ${PROJECT_ROOT}/lib/Epub/src/Epub/css
  ${PROJECT_ROOT}/lib/PageCache/src
  ${PROJECT_ROOT}/lib/FsHelpers/src
  ${PROJECT_ROOT}/lib/ScriptDetector/src
  ${PROJECT_ROOT}/lib/Markdown/src
  ${PROJECT_ROOT}/lib/Txt/src
  ${PROJECT_ROOT}/lib/Fb2/src
  ${PROJECT_ROOT}/lib/Html/src
  ${PROJECT_ROOT}/lib/GfxRenderer/src
  ${PROJECT_ROOT}/lib/EpdFont/src
  ${PROJECT_ROOT}/lib/EpdFont/src/builtinFonts
  ${PROJECT_ROOT}/lib/ExternalFont/src
  ${PROJECT_ROOT}/lib/ArabicShaper/src
  ${PROJECT_ROOT}/lib/ThaiShaper/src
  ${PROJECT_ROOT}/lib/ZipFile/src
  ${PROJECT_ROOT}/lib/InflateReader/src
  ${PROJECT_ROOT}/lib/uzlib/src
  ${PROJECT_ROOT}/lib/Hyphenation/src
  ${PROJECT_ROOT}/lib/Html5/src
  ${PROJECT_ROOT}/lib/ImageConverter/src
)

# Force-include Arduino.h like PlatformIO does (provides cstdint, Print, etc.)
target_compile_options(reader-test PRIVATE -include Arduino.h)

target_link_libraries(reader-test PRIVATE EXPAT::EXPAT)
