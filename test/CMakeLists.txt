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
  ${PROJECT_ROOT}/lib/Encoding/src
  ${PROJECT_ROOT}/lib/Utf8/src
  ${PROJECT_ROOT}/lib/Serialization/src
  ${PROJECT_ROOT}/lib/AsyncTask/src
  ${PROJECT_ROOT}/lib/EInkDisplay/include
  ${PROJECT_ROOT}/lib/Memory/src
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
  ${PROJECT_ROOT}/lib/ZipFile/src
  ${PROJECT_ROOT}/lib/InflateReader/src
  ${PROJECT_ROOT}/lib/uzlib/src
  ${PROJECT_ROOT}/lib/BoardSupport/include
  ${PROJECT_ROOT}/lib/BatteryMonitor/include
)

# Common test helpers
set(TEST_HELPERS
  ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/common/test_font_data.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/platform_stubs.cpp
  ${PROJECT_ROOT}/lib/BoardSupport/src/TouchTransform.cpp
)

# Auto-discover and build tests
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)

foreach(TEST_SRC ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

  if(TEST_NAME STREQUAL "DigitalInputTest")
    foreach(INPUT_TARGET X4PRO X4CLASSIC)
      set(INPUT_TEST DigitalInput${INPUT_TARGET}Test)
      add_executable(${INPUT_TEST} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/InputManager/src/InputManager.cpp
      ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/mocks/platform_stubs.cpp)
      target_include_directories(${INPUT_TEST} PRIVATE ${PROJECT_ROOT}/lib/InputManager/include)
      target_compile_definitions(${INPUT_TEST} PRIVATE PAPYRIX_TARGET_${INPUT_TARGET}=1)
      set_target_properties(${INPUT_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  endif()
  # CMake builds BoardProfileTest once for each target.
  # Each executable checks one profile set.
  if(TEST_NAME STREQUAL "BoardProfileTest")
    add_executable(BoardProfileC3Test ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    add_executable(BoardProfileX4ProTest ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    add_executable(BoardProfileX4ClassicTest ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    target_compile_definitions(BoardProfileX4ClassicTest PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
    target_compile_definitions(BoardProfileC3Test PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
    target_compile_definitions(BoardProfileX4ProTest PRIVATE PAPYRIX_TARGET_X4PRO=1)
    foreach(BOARD_PROFILE_EXE BoardProfileC3Test BoardProfileX4ProTest BoardProfileX4ClassicTest)
      target_include_directories(${BOARD_PROFILE_EXE} PRIVATE
        ${PROJECT_ROOT}/lib/BoardSupport/include
      )
      set_target_properties(${BOARD_PROFILE_EXE} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
    endforeach()
    continue()
  endif()

  # Special handling for tests that need additional source files
  if(TEST_NAME STREQUAL "X4ClassicPanelPolicyTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/X4ClassicPanelPolicy.cpp)
  elseif(TEST_NAME STREQUAL "X4ClassicDisplayProbeTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/X4ClassicDisplayProbe.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/mocks/platform_stubs.cpp)
  elseif(TEST_NAME STREQUAL "BoardSelectorTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardSelector.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
      ${PROJECT_ROOT}/lib/EInkDisplay/include
    )
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
  elseif(TEST_NAME STREQUAL "ElementsBatteryTest" OR TEST_NAME STREQUAL "ElementsLogoTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/Elements.cpp
      ${TEST_HELPERS})
    target_compile_options(${TEST_NAME} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(${TEST_NAME} PRIVATE -Wl,--gc-sections)
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/I18n/src)
  elseif(TEST_NAME STREQUAL "CrashDebugTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/CrashDebug.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE RTC_DATA_ATTR=)
  elseif(TEST_NAME STREQUAL "QueuedButtonInputTest")
    foreach(INPUT_TARGET XTEINK_C3 X4CLASSIC)
      set(INPUT_TEST QueuedButton${INPUT_TARGET}Test)
      add_executable(${INPUT_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/src/hal/Input.cpp
        ${PROJECT_ROOT}/src/MappedInputManager.cpp
        ${PROJECT_ROOT}/lib/InputManager/src/InputManager.cpp
        ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${TEST_HELPERS})
      target_include_directories(${INPUT_TEST} PRIVATE ${PROJECT_ROOT}/lib/InputManager/include)
      target_compile_definitions(${INPUT_TEST} PRIVATE PAPYRIX_TARGET_${INPUT_TARGET}=1)
      set_target_properties(${INPUT_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "HalTouchInputTest" OR TEST_NAME STREQUAL "HalButtonInputTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Input.cpp
      ${PROJECT_ROOT}/src/MappedInputManager.cpp
      ${PROJECT_ROOT}/lib/InputManager/src/InputManager.cpp
      ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/InputManager/include)
    if(TEST_NAME STREQUAL "HalButtonInputTest")
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
    else()
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1)
    endif()
  elseif(TEST_NAME STREQUAL "PanelDispatchTest")
    foreach(PANEL_TARGET X4PRO X4CLASSIC)
      set(PANEL_TEST PanelDispatch${PANEL_TARGET}Test)
      add_executable(${PANEL_TEST} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Display.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X3Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8179X4ProDriver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X4ProDriver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279SpiBus.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS})
      target_include_directories(${PANEL_TEST} BEFORE PRIVATE ${PROJECT_ROOT}/lib/EInkDisplay/include)
      target_compile_definitions(${PANEL_TEST} PRIVATE PAPYRIX_TARGET_${PANEL_TARGET}=1 EINK_DISPLAY_SINGLE_BUFFER_MODE=1)
      set_target_properties(${PANEL_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "HardwareRecoveryTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareRecovery.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
      ${PROJECT_ROOT}/lib/EInkDisplay/include
    )
  elseif(TEST_NAME STREQUAL "DisplayRecoveryTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Recovery.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "X4ProBoardTest" OR TEST_NAME STREQUAL "X4ClassicBoardTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/FrontLightBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BootHardware.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/PowerPolicy.cpp
      ${PROJECT_ROOT}/lib/SDCardManager/src/SDPowerControl.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
      ${PROJECT_ROOT}/lib/SDCardManager/include
    )
    if(TEST_NAME STREQUAL "X4ClassicBoardTest")
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
    else()
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1)
    endif()
  elseif(TEST_NAME STREQUAL "PowerPolicyTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BootHardware.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/PowerPolicy.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/SDCardManager/src/SDPowerControl.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
      ${PROJECT_ROOT}/lib/SDCardManager/include
    )
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
  elseif(TEST_NAME STREQUAL "TouchTransformTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
    )
  elseif(TEST_NAME STREQUAL "TapClassifierTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
      ${PROJECT_ROOT}/src
    )
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1)
  elseif(TEST_NAME STREQUAL "BackgroundTaskTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ThaiShaperTest")
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
  elseif(TEST_NAME STREQUAL "FsHelpersNormaliseTest" OR TEST_NAME STREQUAL "NaturalSortTest" OR
          TEST_NAME STREQUAL "BoundedNameQueueTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "FileIndexTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/FileIndex/src/FileIndex.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/FileIndex/src
    )
  elseif(TEST_NAME MATCHES "^ScriptDetector.*Test$")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "MarkdownRtlTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "MarkdownBatchTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/src/MarkdownParser.cpp
      ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/Encoding/src
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
      ${PROJECT_ROOT}/lib/PageCache/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/ArabicShaper/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
  elseif(TEST_NAME STREQUAL "MarkdownLargeParagraphTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/src/MarkdownParser.cpp
      ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/Encoding/src
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
      ${PROJECT_ROOT}/lib/PageCache/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/ArabicShaper/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
  elseif(TEST_NAME MATCHES "^MarkdownParser.*Test$")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "CssParserTest" OR TEST_NAME STREQUAL "CssParserClassTest")
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
      ${PROJECT_ROOT}/lib/Fb2/src
      ${PROJECT_ROOT}/lib/Html/src
      ${PROJECT_ROOT}/lib/Markdown/src
      ${PROJECT_ROOT}/lib/Txt/src
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc
    )
  elseif(TEST_NAME STREQUAL "ExitToUiTransitionTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/ExitToUiTransition.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "CoreNavigationTouchTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/I18n/src
    )
  elseif(TEST_NAME STREQUAL "ReaderOverlayTouchTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/I18n/src
    )
  elseif(TEST_NAME STREQUAL "BootSleepViewsTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/views/BootSleepViews.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ServiceViewsTouchTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/I18n/src
    )
  elseif(TEST_NAME STREQUAL "TouchLayoutTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ReaderButtonDispatchTest" OR TEST_NAME STREQUAL "ReaderTapZonesTest" OR
         TEST_NAME STREQUAL "TocPowerButtonTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/ReaderButtonDispatcher.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ContentHandleThumbnailTest" OR TEST_NAME STREQUAL "EpubIndexAnchorsTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/ContentHandle.cpp
      ${PROJECT_ROOT}/src/content/ContentTypes.cpp
      ${PROJECT_ROOT}/src/content/EpubProvider.cpp
      ${PROJECT_ROOT}/src/content/XtcProvider.cpp
      ${PROJECT_ROOT}/src/content/TxtProvider.cpp
      ${PROJECT_ROOT}/src/content/MarkdownProvider.cpp
      ${PROJECT_ROOT}/src/content/Fb2Provider.cpp
      ${PROJECT_ROOT}/src/content/HtmlProvider.cpp
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc/XtcParser.cpp
      ${PROJECT_ROOT}/lib/Xtc/src/XtcCoverHelper.cpp
      # Content handlers
      ${PROJECT_ROOT}/lib/Txt/src/Txt.cpp
      ${PROJECT_ROOT}/lib/Markdown/src/Markdown.cpp
      ${PROJECT_ROOT}/lib/Markdown/src/MarkdownParser.cpp
      ${PROJECT_ROOT}/lib/Markdown/src/md_parser.c
      ${PROJECT_ROOT}/lib/Fb2/src/Fb2.cpp
      ${PROJECT_ROOT}/lib/Fb2/src/Fb2Parser.cpp
      ${PROJECT_ROOT}/lib/Html/src/Html.cpp
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
      # Cover / thumbnail / image chain
      ${PROJECT_ROOT}/lib/GfxRenderer/src/CoverHelpers.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/HomeThumbnail.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/PngToBmpConverter/src/PngToBmpConverter.cpp
      ${PROJECT_ROOT}/lib/JpegToBmpConverter/src/JpegToBmpConverter.cpp
      # Text layout / fonts / hyphenation (needed by the EPUB chapter parser)
      ${PROJECT_ROOT}/lib/PageCache/src/HtmlParser.cpp
      ${PROJECT_ROOT}/lib/PageCache/src/EpubChapterParser.cpp
      ${PROJECT_ROOT}/lib/PageCache/src/PlainTextParser.cpp
      ${PROJECT_ROOT}/lib/PageCache/src/PageCache.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/ImageBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontLoader.cpp
      ${PROJECT_ROOT}/lib/ExternalFont/src/ExternalFont.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp
      # Encoding / zip support
      ${PROJECT_ROOT}/lib/Html5/src/Html5Normalizer.cpp
      ${PROJECT_ROOT}/lib/Html5/src/HtmlSplitter.cpp
      ${PROJECT_ROOT}/lib/ZipFile/src/ZipFile.cpp
      ${PROJECT_ROOT}/lib/InflateReader/src/InflateReader.cpp
      ${PROJECT_ROOT}/lib/uzlib/src/tinflate.c
      ${PROJECT_ROOT}/lib/uzlib/src/adler32.c
      ${PROJECT_ROOT}/lib/uzlib/src/crc32.c
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1 XML_GE=0 XML_DTD)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/content/mocks
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/jpegconverter/mocks
      ${PROJECT_ROOT}/lib/PngToBmpConverter/src
      ${PROJECT_ROOT}/lib/JpegToBmpConverter/src
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/src/content
      ${PROJECT_ROOT}/lib/Xtc/src
      ${PROJECT_ROOT}/lib/Fb2/src
      ${PROJECT_ROOT}/lib/Html/src
      ${PROJECT_ROOT}/lib/Markdown/src
      ${PROJECT_ROOT}/lib/Txt/src
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc
      ${PROJECT_ROOT}/lib/Epub/src
      ${PROJECT_ROOT}/lib/Epub/src/Epub
      ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers
      ${PROJECT_ROOT}/lib/Epub/src/Epub/css
      ${PROJECT_ROOT}/lib/ZipFile/src
      ${PROJECT_ROOT}/lib/InflateReader/src
      ${PROJECT_ROOT}/lib/uzlib/src
      ${PROJECT_ROOT}/lib/Encoding/src
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
      ${PROJECT_ROOT}/lib/PageCache/src
      ${PROJECT_ROOT}/lib/Html5/src
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
  elseif(TEST_NAME STREQUAL "XtcProviderCoverTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/XtcProvider.cpp
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc/XtcParser.cpp
      ${PROJECT_ROOT}/lib/Xtc/src/XtcCoverHelper.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/HomeThumbnail.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Xtc/src
      ${PROJECT_ROOT}/lib/Fb2/src
      ${PROJECT_ROOT}/lib/Html/src
      ${PROJECT_ROOT}/lib/Markdown/src
      ${PROJECT_ROOT}/lib/Txt/src
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc
      ${PROJECT_ROOT}/src/content
      ${PROJECT_ROOT}/lib/Utf8/src
    )
  elseif(TEST_NAME STREQUAL "XtcParserTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc/XtcParser.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Xtc/src
      ${PROJECT_ROOT}/lib/Fb2/src
      ${PROJECT_ROOT}/lib/Html/src
      ${PROJECT_ROOT}/lib/Markdown/src
      ${PROJECT_ROOT}/lib/Txt/src
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc
    )
  elseif(TEST_NAME STREQUAL "XtcPageRendererTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/rendering/XtcPageRenderer.cpp
      ${PROJECT_ROOT}/lib/Xtc/src/Xtc/XtcParser.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/xtc/mocks
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/src/rendering
      ${PROJECT_ROOT}/lib/Xtc/src
      ${PROJECT_ROOT}/lib/Fb2/src
      ${PROJECT_ROOT}/lib/Html/src
      ${PROJECT_ROOT}/lib/Markdown/src
      ${PROJECT_ROOT}/lib/Txt/src
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
  elseif(TEST_NAME STREQUAL "SpineSplitTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
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
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
  elseif(TEST_NAME STREQUAL "TocParserTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0)
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
  elseif(TEST_NAME STREQUAL "Fb2StyleTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0)
    target_link_libraries(${TEST_NAME} PRIVATE EXPAT::EXPAT)
  elseif(TEST_NAME STREQUAL "Fb2Base64Test")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
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
  elseif(TEST_NAME STREQUAL "ZipFileErrorPathTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ZipFile/src/ZipFile.cpp
      ${PROJECT_ROOT}/lib/InflateReader/src/InflateReader.cpp
      ${PROJECT_ROOT}/lib/uzlib/src/tinflate.c
      ${PROJECT_ROOT}/lib/uzlib/src/adler32.c
      ${PROJECT_ROOT}/lib/uzlib/src/crc32.c
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/ZipFile/src
      ${PROJECT_ROOT}/lib/InflateReader/src
      ${PROJECT_ROOT}/lib/uzlib/src
    )
  elseif(TEST_NAME STREQUAL "PngOneBitTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/PngToBmpConverter/src/PngToBmpConverter.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${PROJECT_ROOT}/lib/PngToBmpConverter/src
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
  elseif(TEST_NAME STREQUAL "JpegThumbnailCancellationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/JpegToBmpConverter/src/JpegToBmpConverter.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/jpegconverter/mocks
      ${PROJECT_ROOT}/lib/JpegToBmpConverter/src
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
  elseif(TEST_NAME STREQUAL "BmpConversionTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
  elseif(TEST_NAME STREQUAL "ImageConverterSignatureTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
  elseif(TEST_NAME STREQUAL "ImageFileViewTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/ImageFileView.cpp
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
      ${TEST_HELPERS}
    )
    target_compile_options(${TEST_NAME} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(${TEST_NAME} PRIVATE -Wl,--gc-sections)
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
  elseif(TEST_NAME STREQUAL "PackedCoverReducerTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "HomeThumbnailTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/GfxRenderer/src/HomeThumbnail.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "BitmapRowAccessTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "BmpPolarityRoundTripTest" OR TEST_NAME STREQUAL "BitmapTurnCacheTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/GfxRenderer/src/GfxRenderer.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${PROJECT_ROOT}/lib/GfxRenderer/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    if(TEST_NAME STREQUAL "BitmapTurnCacheTest")
      target_sources(${TEST_NAME} PRIVATE
        ${PROJECT_ROOT}/lib/RenderTypes/src/BitmapTurnCache.cpp
        ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/ImageBlock.cpp
        ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
        ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
        ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
        ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
        ${PROJECT_ROOT}/lib/EpdFont/src/StreamingEpdFont.cpp
        ${PROJECT_ROOT}/lib/ExternalFont/src/ExternalFont.cpp
        ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiClusterBuilder.cpp
        ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiCharacter.cpp
        ${PROJECT_ROOT}/lib/ArabicShaper/src/ArabicShaper.cpp
        ${PROJECT_ROOT}/lib/ArabicShaper/src/ArabicCharacter.cpp
      )
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1)
      target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/RenderTypes/src/blocks)
    endif()
    if(MSVC)
      target_compile_options(${TEST_NAME} PRIVATE /Gy)
      target_link_options(${TEST_NAME} PRIVATE /OPT:REF)
    elseif(APPLE)
      target_compile_options(${TEST_NAME} PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(${TEST_NAME} PRIVATE -Wl,-dead_strip)
    else()
      target_compile_options(${TEST_NAME} PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(${TEST_NAME} PRIVATE -Wl,--gc-sections)
    endif()
  elseif(TEST_NAME STREQUAL "ParsedTextAbortTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    # Local mocks (GfxRenderer.h, Hyphenation.h) override real headers
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/parsedtext/mocks
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
    )
  elseif(TEST_NAME STREQUAL "ParsedTextWindowTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    # Local mocks (GfxRenderer.h, Hyphenation.h) override real headers
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/parsedtext/mocks
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
    )
  elseif(TEST_NAME STREQUAL "ButtonHintDimensionsTest")
    add_executable(${TEST_NAME} ${TEST_SRC} ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${PROJECT_ROOT}/lib/GfxRenderer/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
  elseif(TEST_NAME STREQUAL "WrappedTextTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/ui/WrappedText.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "BookmarkManagerTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/BookmarkManager.cpp
      ${PROJECT_ROOT}/src/hal/Storage.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "CapacityContractsTest" OR TEST_NAME STREQUAL "BookmarkListViewTest")
    add_executable(${TEST_NAME} ${TEST_SRC} ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/I18n/src
    )
  elseif(TEST_NAME STREQUAL "BookStatsViewTest" OR TEST_NAME STREQUAL "ReaderMenuViewTest")
    add_executable(${TEST_NAME} ${TEST_SRC} ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/I18n/src
    )
  elseif(TEST_NAME STREQUAL "RecentBooksIoTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/RecentBooksStore.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ReadingStatsStoreTest" OR TEST_NAME STREQUAL "ReadingStatsStoreLoadFailureTest" OR
         TEST_NAME STREQUAL "ReadingStatsStoreIoTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/ReadingStatsStore.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ReadingSessionTrackerTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/ReadingSessionTracker.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "StateMachineTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/StateMachine.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ContentTypeDetectionTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/ContentTypes.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "WebFileNameValidationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/network/WebFileNameValidation.cpp
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "CpuDriverTest" OR TEST_NAME STREQUAL "WifiSessionTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Cpu.cpp
      ${TEST_HELPERS}
    )
    if(TEST_NAME STREQUAL "CpuDriverTest")
      target_compile_definitions(${TEST_NAME} PRIVATE F_CPU=240000000L)
    endif()
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/src
    )
  elseif(TEST_NAME STREQUAL "BatteryMonitorPercentageTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/BatteryMonitor.cpp
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/Cw2017Backend.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BatteryMonitor/include
    )
  elseif(TEST_NAME STREQUAL "TxtBatchTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/PageCache/src/PlainTextParser.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/Encoding/src
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
      ${PROJECT_ROOT}/lib/PageCache/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/ArabicShaper/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
  elseif(TEST_NAME STREQUAL "TxtLargeParagraphTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/PageCache/src/PlainTextParser.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
      ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8Nfc.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenation.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/HyphenationCommon.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/Hyphenator.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LanguageRegistry.cpp
      ${PROJECT_ROOT}/lib/Hyphenation/src/LiangHyphenation.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/Encoding/src
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/RenderTypes/src
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks
      ${PROJECT_ROOT}/lib/PageCache/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/ArabicShaper/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
  elseif(TEST_NAME STREQUAL "TxtRtlTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "EmergencyBootTransitionTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/EmergencyBootTransition.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "SettingsSerializationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/SettingsSerialization.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "PageCacheIntegrationTest" OR TEST_NAME STREQUAL "PageCacheWatchdogTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/PageCache/src/PageCache.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
    if(TEST_NAME STREQUAL "PageCacheWatchdogTest")
      target_compile_definitions(${TEST_NAME} PRIVATE ARDUINO=1)
    endif()
  elseif(TEST_NAME STREQUAL "PageSerializationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "BookMetadataCacheTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/src/Epub/BookMetadataCache.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "Html5NormalizerTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Html5/src/Html5Normalizer.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Html5/src
    )
  elseif(TEST_NAME STREQUAL "FirmwareUpdaterTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/core/FirmwareUpdater.cpp
      ${PROJECT_ROOT}/src/core/OtaBootSwitch.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "G5ImageCacheTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Group5/src/G5ImageCache.cpp
      ${PROJECT_ROOT}/lib/Group5/src/Group5.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Group5/src
    )
  elseif(TEST_NAME STREQUAL "Utf8Test")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "GlobalPageMetricsTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/GlobalPageMetrics.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ReaderNavigationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/content/ReaderNavigation.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/navigation/mocks
    )
  elseif(TEST_NAME STREQUAL "Ssd1677DriverTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/src
    )
  elseif(TEST_NAME STREQUAL "X3DisplayProbeTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/X3DisplayProbeClassifier.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/include
    )
  elseif(TEST_NAME STREQUAL "X3ControllerPolicyTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/X3ControllerPolicy.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/include
      ${PROJECT_ROOT}/lib/BoardSupport/include
    )
  elseif(TEST_NAME STREQUAL "Uc8279SpiBusTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279SpiBus.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/src
    )
  elseif(TEST_NAME STREQUAL "Uc8279X4ProDriverTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X4ProDriver.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/src
    )
  elseif(TEST_NAME STREQUAL "Uc8179X4ProDriverTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8179X4ProDriver.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/src
    )
  elseif(TEST_NAME STREQUAL "Uc8279X3DriverTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X3Driver.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/EInkDisplay/include
      ${PROJECT_ROOT}/lib/EInkDisplay/src
    )
  elseif(TEST_NAME STREQUAL "UsbPolicyTest" OR TEST_NAME STREQUAL "FrontLightPolicyTest" OR
         TEST_NAME STREQUAL "BatteryChargePolicyTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
    )
  elseif(TEST_NAME STREQUAL "ClockTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Clock.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/src/hal
      ${PROJECT_ROOT}/lib/BoardSupport/include
    )
  elseif(TEST_NAME STREQUAL "RtcBackendTest")
    foreach(RTC_TEST ${TEST_NAME} RtcBm8563Test RtcClassicTest)
      add_executable(${RTC_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/lib/BoardSupport/src/RtcBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${TEST_HELPERS})
      set_target_properties(${RTC_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
    endforeach()
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
    target_compile_definitions(RtcBm8563Test PRIVATE PAPYRIX_TARGET_X4PRO=1)
    target_compile_definitions(RtcClassicTest PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
    continue()
  elseif(TEST_NAME STREQUAL "Cw2017PolicyTest")
    foreach(GAUGE_TARGET X4PRO X4CLASSIC)
      set(GAUGE_TEST Cw2017${GAUGE_TARGET}Test)
      add_executable(${GAUGE_TEST}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/BatteryMonitor.cpp
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/Cw2017Backend.cpp
      ${TEST_HELPERS}
    )
      target_compile_definitions(${GAUGE_TEST} PRIVATE PAPYRIX_TARGET_${GAUGE_TARGET}=1)
      set_target_properties(${GAUGE_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "StorageTransportTest")
    foreach(STORAGE_TARGET C3 X4Pro X4Classic)
      add_executable(StorageTransport${STORAGE_TARGET}Test ${TEST_SRC}
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${TEST_HELPERS})
      target_include_directories(StorageTransport${STORAGE_TARGET}Test PRIVATE
        ${PROJECT_ROOT}/lib/BoardSupport/include
        ${PROJECT_ROOT}/lib/SDCardManager/include)
      set_target_properties(StorageTransport${STORAGE_TARGET}Test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
    endforeach()
    target_compile_definitions(StorageTransportC3Test PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
    target_compile_definitions(StorageTransportX4ProTest PRIVATE PAPYRIX_TARGET_X4PRO=1)
    target_compile_definitions(StorageTransportX4ClassicTest PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
    continue()
  elseif(TEST_NAME STREQUAL "SDPowerControlTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/SDCardManager/src/SDPowerControl.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/SDCardManager/include
    )
  elseif(TEST_NAME STREQUAL "GfxRendererBackgroundHintTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/GfxRenderer/src/GfxRenderer.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${PROJECT_ROOT}/lib/GfxRenderer/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    if(MSVC)
      target_compile_options(${TEST_NAME} PRIVATE /Gy)
      target_link_options(${TEST_NAME} PRIVATE /OPT:REF)
    elseif(APPLE)
      target_compile_options(${TEST_NAME} PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(${TEST_NAME} PRIVATE -Wl,-dead_strip)
    else()
      target_compile_options(${TEST_NAME} PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(${TEST_NAME} PRIVATE -Wl,--gc-sections)
    endif()
  elseif(TEST_NAME STREQUAL "LocalsendTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Localsend/src/LocalsendHttp.cpp
      ${PROJECT_ROOT}/lib/Localsend/src/LocalsendService.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/Localsend/src)
  elseif(TEST_NAME STREQUAL "IppTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Ipp/src/IppParser.cpp
      ${PROJECT_ROOT}/lib/Ipp/src/IppWriter.cpp
      ${PROJECT_ROOT}/lib/Ipp/src/IppPrintService.cpp
      ${PROJECT_ROOT}/lib/Ipp/src/HttpIppConnection.cpp
      ${PROJECT_ROOT}/lib/Ipp/src/RasterDecoder.cpp
      ${PROJECT_ROOT}/lib/Ipp/src/PageScaler.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/Ipp/src)
  elseif(TEST_NAME STREQUAL "WifiCredentialOrderTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/network/WifiCredentialStore.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/SDCardManager/include)
  elseif(TEST_NAME STREQUAL "SdFreeSpaceTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/SDCardManager/include)
  else()
    add_executable(${TEST_NAME} ${TEST_SRC} ${TEST_HELPERS})
  endif()
  if(TEST_NAME STREQUAL "StreamingEpdFontS3Test" OR TEST_NAME STREQUAL "ParserScratchTest")
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1)
  endif()

  target_include_directories(${TEST_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  set_target_properties(${TEST_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
endforeach()

message(STATUS "Configured ${TEST_SOURCES} tests")
