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

include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
# Match the firmware's pngle 1.0.2 dependency.
FetchContent_Declare(pngle
  URL https://codeload.github.com/kikuchan/pngle/tar.gz/745944757fc3f7c75cc65bdd197ef20aa30ad72a
  URL_HASH SHA256=3c0e6c5a4b70b6e207e7aa2a953caeb4d1360179ed6dbab2a295dca6af8e1b37)
FetchContent_MakeAvailable(pngle)
set(PNGLE_DIR "${pngle_SOURCE_DIR}/src")

foreach(TEST_SRC ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

  if(TEST_NAME STREQUAL "ProgressiveJpegFactoryTest")
    foreach(IMAGE_TARGET X4PRO X4CLASSIC XTEINK_C3 STICKY MURPHY_M4)
      set(IMAGE_TEST ProgressiveJpegFactory${IMAGE_TARGET}Test)
      add_executable(${IMAGE_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
        ${PROJECT_ROOT}/lib/ImageConverter/src/ProgressiveJpegImageConverter.cpp
        ${PROJECT_ROOT}/lib/ProgressiveJpeg/src/ProgressiveJpeg.cpp
        ${PROJECT_ROOT}/lib/JpegToBmpConverter/src/JpegToBmpConverter.cpp
        ${PROJECT_ROOT}/lib/PngToBmpConverter/src/PngToBmpConverter.cpp
        ${PROJECT_ROOT}/lib/picojpeg/src/picojpeg.c
        ${PNGLE_DIR}/pngle.c ${PNGLE_DIR}/miniz.c
        ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
        ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
        ${TEST_HELPERS})
      target_include_directories(${IMAGE_TEST} BEFORE PRIVATE
        ${PROJECT_ROOT}/lib/ImageConverter/src ${PROJECT_ROOT}/lib/ProgressiveJpeg/src
        ${PROJECT_ROOT}/lib/JpegToBmpConverter/src ${PROJECT_ROOT}/lib/PngToBmpConverter/src
        ${PROJECT_ROOT}/lib/picojpeg/src ${PNGLE_DIR})
      target_compile_definitions(${IMAGE_TEST} PRIVATE PAPYRIX_TARGET_${IMAGE_TARGET}=1
        PROGRESSIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/progressive-jpeg")
      if(IMAGE_TARGET STREQUAL "MURPHY_M4")
        target_sources(${IMAGE_TEST} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
      endif()
      set_target_properties(${IMAGE_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  endif()
  if(TEST_NAME STREQUAL "DigitalInputTest")
    foreach(INPUT_TARGET X4PRO X4CLASSIC STICKY MURPHY_M4)
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
  if(TEST_NAME STREQUAL "MurphyM4BatchTest")
    add_executable(${TEST_NAME} ${TEST_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    target_sources(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
    set_target_properties(${TEST_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    continue()
  endif()
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
    add_executable(BoardProfileStickyTest ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    target_compile_definitions(BoardProfileStickyTest PRIVATE PAPYRIX_TARGET_STICKY=1)
    add_executable(BoardProfileMurphyM4Test ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    target_compile_definitions(BoardProfileMurphyM4Test PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
    target_compile_definitions(BoardProfileX4ClassicTest PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
    target_compile_definitions(BoardProfileC3Test PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
    target_compile_definitions(BoardProfileX4ProTest PRIVATE PAPYRIX_TARGET_X4PRO=1)
    foreach(BOARD_PROFILE_EXE BoardProfileC3Test BoardProfileX4ProTest BoardProfileX4ClassicTest BoardProfileStickyTest BoardProfileMurphyM4Test)
      target_include_directories(${BOARD_PROFILE_EXE} PRIVATE
        ${PROJECT_ROOT}/lib/BoardSupport/include
      )
      set_target_properties(${BOARD_PROFILE_EXE} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
    endforeach()
    continue()
  endif()

  # Special handling for tests that need additional source files
  if(TEST_NAME STREQUAL "ProgressiveJpegConverterTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ProgressiveJpeg/src/ProgressiveJpeg.cpp
      ${PROJECT_ROOT}/lib/ImageConverter/src/ProgressiveJpegImageConverter.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${PROJECT_ROOT}/lib/ImageConverter/src ${PROJECT_ROOT}/lib/ProgressiveJpeg/src)
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1
      PROGRESSIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/progressive-jpeg")
    set_target_properties(${TEST_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    continue()
  endif()
  if(TEST_NAME STREQUAL "ProgressiveJpegTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/ProgressiveJpeg/src/ProgressiveJpeg.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/common/test_utils.cpp)
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/ProgressiveJpeg/src)
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_X4PRO=1
      PROGRESSIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/progressive-jpeg")
  elseif(TEST_NAME STREQUAL "StickyDisplayProbeTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/StickyDisplayProbe.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardSelector.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareRecovery.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_STICKY=1 ARDUINO=10819)
  elseif(TEST_NAME STREQUAL "X4ClassicPanelPolicyTest")
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
    foreach(INPUT_TARGET XTEINK_C3 X4CLASSIC STICKY)
      set(INPUT_TEST QueuedButton${INPUT_TARGET}Test)
      add_executable(${INPUT_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/src/hal/Input.cpp
        ${PROJECT_ROOT}/src/MappedInputManager.cpp
        ${PROJECT_ROOT}/src/core/ReaderButtonDispatcher.cpp
        ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
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
    set(HAL_INPUT_TESTS ${TEST_NAME})
    if(TEST_NAME STREQUAL "HalTouchInputTest")
      list(APPEND HAL_INPUT_TESTS HalTouchStickyTest HalTouchMurphyM4Test)
    else()
      list(APPEND HAL_INPUT_TESTS HalButtonStickyTest HalButtonMurphyM4Test)
    endif()
    foreach(HAL_INPUT_TEST ${HAL_INPUT_TESTS})
      add_executable(${HAL_INPUT_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/src/hal/Input.cpp
        ${PROJECT_ROOT}/src/MappedInputManager.cpp
        ${PROJECT_ROOT}/lib/InputManager/src/InputManager.cpp
        ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${TEST_HELPERS})
      target_include_directories(${HAL_INPUT_TEST} PRIVATE ${PROJECT_ROOT}/lib/InputManager/include)
      if(HAL_INPUT_TEST STREQUAL "HalButtonInputTest")
        target_compile_definitions(${HAL_INPUT_TEST} PRIVATE PAPYRIX_TARGET_X4CLASSIC=1)
      elseif(HAL_INPUT_TEST STREQUAL "HalTouchMurphyM4Test" OR HAL_INPUT_TEST STREQUAL "HalButtonMurphyM4Test")
        target_compile_definitions(${HAL_INPUT_TEST} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
        target_sources(${HAL_INPUT_TEST} PRIVATE
          ${PROJECT_ROOT}/lib/BoardSupport/src/Ft6336uBackend.cpp
          ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
          ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
      elseif(HAL_INPUT_TEST STREQUAL "HalTouchStickyTest" OR HAL_INPUT_TEST STREQUAL "HalButtonStickyTest")
        target_compile_definitions(${HAL_INPUT_TEST} PRIVATE PAPYRIX_TARGET_STICKY=1)
      else()
        target_compile_definitions(${HAL_INPUT_TEST} PRIVATE PAPYRIX_TARGET_X4PRO=1)
      endif()
      set_target_properties(${HAL_INPUT_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "MurphyM4TouchTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Input.cpp
      ${PROJECT_ROOT}/src/MappedInputManager.cpp
      ${PROJECT_ROOT}/lib/InputManager/src/InputManager.cpp
      ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/Ft6336uBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/RtcBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Display.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X3Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Ssd2677Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279SpiBus.cpp
      ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} BEFORE PRIVATE ${PROJECT_ROOT}/lib/EInkDisplay/include)
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/InputManager/include)
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1 EINK_DISPLAY_SINGLE_BUFFER_MODE=1)
  elseif(TEST_NAME STREQUAL "TouchTransformTest")
    foreach(TRANSFORM_TEST TouchTransformTest TouchTransformMurphyM4Test)
      add_executable(${TRANSFORM_TEST} ${TEST_SRC} ${TEST_HELPERS})
      if(TRANSFORM_TEST STREQUAL "TouchTransformMurphyM4Test")
        target_sources(${TRANSFORM_TEST} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
        target_compile_definitions(${TRANSFORM_TEST} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
      endif()
      set_target_properties(${TRANSFORM_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "MurphyM4Ssd1677Test")
    foreach(M4_DISPLAY_TEST MurphyM4Ssd1677Test MurphyM4Ssd1677Batch1Test)
      add_executable(${M4_DISPLAY_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/lib/EInkDisplay/src/Display.cpp
        ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X3Driver.cpp
        ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279SpiBus.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/Ft6336uBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp
        ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/GfxRenderer.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
        ${TEST_HELPERS})
      target_include_directories(${M4_DISPLAY_TEST} BEFORE PRIVATE
        ${PROJECT_ROOT}/lib/EInkDisplay/include ${PROJECT_ROOT}/lib/GfxRenderer/src
        ${PROJECT_ROOT}/lib/ExternalFont/src ${PROJECT_ROOT}/lib/ThaiShaper/src)
      target_compile_definitions(${M4_DISPLAY_TEST} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1 EINK_DISPLAY_SINGLE_BUFFER_MODE=1)
      target_compile_options(${M4_DISPLAY_TEST} PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(${M4_DISPLAY_TEST} PRIVATE -Wl,--gc-sections)
      set_target_properties(${M4_DISPLAY_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    target_compile_definitions(MurphyM4Ssd1677Batch1Test PRIVATE TEST_M4_BATCH_FIRST=1)
    continue()
  elseif(TEST_NAME STREQUAL "StickySsd1677Test")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Display.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X3Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Ssd2677Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279SpiBus.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_STICKY=1 EINK_DISPLAY_SINGLE_BUFFER_MODE=1)
    target_include_directories(${TEST_NAME} BEFORE PRIVATE ${PROJECT_ROOT}/lib/EInkDisplay/include)
  elseif(TEST_NAME STREQUAL "PanelDispatchTest")
    foreach(PANEL_TARGET X4PRO X4CLASSIC STICKY MURPHY_M4)
      set(PANEL_TEST PanelDispatch${PANEL_TARGET}Test)
      add_executable(${PANEL_TEST} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Display.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X3Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Ssd2677Driver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8179X4ProDriver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279X4ProDriver.cpp
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Uc8279SpiBus.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/PowerPolicy.cpp
      ${PROJECT_ROOT}/lib/SDCardManager/src/SDPowerControl.cpp
      ${TEST_HELPERS})
      target_include_directories(${PANEL_TEST} BEFORE PRIVATE ${PROJECT_ROOT}/lib/EInkDisplay/include)
      target_include_directories(${PANEL_TEST} PRIVATE ${PROJECT_ROOT}/lib/SDCardManager/include)
      target_compile_definitions(${PANEL_TEST} PRIVATE PAPYRIX_TARGET_${PANEL_TARGET}=1 EINK_DISPLAY_SINGLE_BUFFER_MODE=1)
      if(PANEL_TARGET STREQUAL "MURPHY_M4")
        target_sources(${PANEL_TEST} PRIVATE
          ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
          ${PROJECT_ROOT}/lib/BoardSupport/src/Ft6336uBackend.cpp
          ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
          ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp
          ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp)
      endif()
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
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
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
  elseif(TEST_NAME STREQUAL "PowerPolicyTest" OR TEST_NAME STREQUAL "StickyBoardTest" OR TEST_NAME STREQUAL "MurphyM4BoardTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/BootHardware.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/PowerPolicy.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/SDCardManager/src/SDPowerControl.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/include
      ${PROJECT_ROOT}/lib/SDCardManager/include
    )
    if(TEST_NAME STREQUAL "StickyBoardTest")
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_STICKY=1)
    elseif(TEST_NAME STREQUAL "MurphyM4BoardTest")
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
      target_sources(${TEST_NAME} PRIVATE
        ${PROJECT_ROOT}/lib/BoardSupport/src/Ft6336uBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp
        ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp)
    else()
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
    endif()
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
      ${PROJECT_ROOT}/lib/uzlib/src/crc32.c
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
  elseif(TEST_NAME STREQUAL "I18nTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/I18n/src/I18n.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/I18n/src)
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
      ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
      ${TEST_HELPERS}
    )
  elseif(TEST_NAME STREQUAL "ContentHandleThumbnailTest" OR TEST_NAME STREQUAL "EpubIndexAnchorsTest" OR
         TEST_NAME STREQUAL "EpubTocRecoveryTest" OR TEST_NAME STREQUAL "EpubCoverDiscoveryTest")
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
      ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/CoverLinkParser.cpp
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
      ${PROJECT_ROOT}/lib/PngToBmpConverter/src
      ${PROJECT_ROOT}/lib/JpegToBmpConverter/src
    )
    if(TEST_NAME STREQUAL "EpubCoverDiscoveryTest")
      target_sources(${TEST_NAME} PRIVATE
        ${PROJECT_ROOT}/lib/picojpeg/src/picojpeg.c
        ${PNGLE_DIR}/pngle.c ${PNGLE_DIR}/miniz.c)
      target_include_directories(${TEST_NAME} BEFORE PRIVATE
        ${PROJECT_ROOT}/lib/picojpeg/src ${PNGLE_DIR})
    else()
      target_include_directories(${TEST_NAME} BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/jpegconverter/mocks)
    endif()
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
  elseif(TEST_NAME STREQUAL "ChapterHtmlImageSuspensionTest" OR TEST_NAME STREQUAL "ChapterAnchorPlacementTest")
    find_package(EXPAT REQUIRED)
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/Epub/src/Epub/parsers/ChapterHtmlSlimParser.cpp
      ${PROJECT_ROOT}/lib/Epub/src/Epub/htmlEntities.cpp
      ${PROJECT_ROOT}/lib/Epub/src/Epub/css/CssParser.cpp
      ${PROJECT_ROOT}/lib/uzlib/src/crc32.c
      ${PROJECT_ROOT}/lib/RenderTypes/src/ParsedText.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/TextBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/blocks/ImageBlock.cpp
      ${PROJECT_ROOT}/lib/RenderTypes/src/Page.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
      ${PROJECT_ROOT}/lib/GfxRenderer/src/PackedCoverReducer.cpp
      ${PROJECT_ROOT}/lib/ImageConverter/src/ImageConverter.cpp
      ${PROJECT_ROOT}/lib/FsHelpers/src/FsHelpers.cpp
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
    target_include_directories(${TEST_NAME} BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/unit/imageconverter/mocks
      ${PROJECT_ROOT}/lib/ImageConverter/src
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/Epub/src
      ${PROJECT_ROOT}/lib/ExternalFont/src
      ${PROJECT_ROOT}/lib/Hyphenation/src
      ${PROJECT_ROOT}/lib/ThaiShaper/src
    )
    target_compile_definitions(${TEST_NAME} PRIVATE XML_GE=0 XML_DTD)
    target_compile_options(${TEST_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include Arduino.h>)
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
    foreach(LOGGING_TEST ${TEST_NAME} LoggingUartTest)
      add_executable(${LOGGING_TEST}
        ${TEST_SRC}
        ${PROJECT_ROOT}/lib/Logging/src/Logging.cpp
      )
      target_include_directories(${LOGGING_TEST} BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/logging
        ${PROJECT_ROOT}/lib/Logging/src
      )
      target_compile_definitions(${LOGGING_TEST} PRIVATE ENABLE_SERIAL_LOG LOG_LEVEL=2)
      set_target_properties(${LOGGING_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    target_compile_definitions(LoggingUartTest PRIVATE TEST_UART_LOG=1)
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
  elseif(TEST_NAME STREQUAL "BmpPolarityRoundTripTest" OR TEST_NAME STREQUAL "BitmapTurnCacheTest" OR
         TEST_NAME STREQUAL "ReaderTouchRenderingTest")
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
    if(TEST_NAME STREQUAL "BitmapTurnCacheTest" OR TEST_NAME STREQUAL "ReaderTouchRenderingTest")
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
    if(TEST_NAME STREQUAL "ReaderTouchRenderingTest")
      target_sources(${TEST_NAME} PRIVATE
        ${PROJECT_ROOT}/src/ui/views/ReaderViews.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/CoverHelpers.cpp
      )
      target_include_directories(${TEST_NAME} PRIVATE
        ${PROJECT_ROOT}/lib/I18n/src
        ${PROJECT_ROOT}/lib/ImageConverter/src
      )
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
  elseif(TEST_NAME STREQUAL "CpuDriverTest" OR TEST_NAME STREQUAL "WifiSessionTest" OR TEST_NAME STREQUAL "MurphyM4CpuTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Cpu.cpp
      ${TEST_HELPERS}
    )
    if(TEST_NAME STREQUAL "CpuDriverTest")
      target_compile_definitions(${TEST_NAME} PRIVATE F_CPU=240000000L)
    endif()
    if(TEST_NAME STREQUAL "MurphyM4CpuTest")
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1 F_CPU=240000000L)
      target_sources(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
    else()
      target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
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
  elseif(TEST_NAME STREQUAL "BatteryMonitorCapacityTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/BatteryMonitor.cpp
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/Cw2017Backend.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/BatteryMonitor/include
    )
  elseif(TEST_NAME STREQUAL "FsCacheInvalidationTest")
    add_executable(${TEST_NAME}
      ${TEST_SRC}
      ${PROJECT_ROOT}/lib/SdFat/src/common/FsCache.cpp
      ${TEST_HELPERS}
    )
    target_include_directories(${TEST_NAME} PRIVATE
      ${PROJECT_ROOT}/lib/SdFat/src
    )
    target_compile_definitions(${TEST_NAME} PRIVATE
      USE_BLOCK_DEVICE_INTERFACE=1 ENABLE_ARDUINO_FEATURES=0 SPI_DRIVER_SELECT=3)
    target_compile_options(${TEST_NAME} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_CURRENT_SOURCE_DIR}/mocks/SdFatHostCompat.h>)
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
  elseif(TEST_NAME STREQUAL "FirmwareBoardTagTest")
    foreach(FIRMWARE_TARGET XTEINK_C3 X4PRO X4CLASSIC STICKY MURPHY_M4)
      set(FIRMWARE_TEST FirmwareBoardTag${FIRMWARE_TARGET}Test)
      add_executable(${FIRMWARE_TEST} ${TEST_SRC} ${PROJECT_ROOT}/src/core/FirmwareBoardTag.cpp ${TEST_HELPERS})
      target_compile_definitions(${FIRMWARE_TEST} PRIVATE PAPYRIX_TARGET_${FIRMWARE_TARGET}=1)
      if(FIRMWARE_TARGET STREQUAL "MURPHY_M4")
        target_sources(${FIRMWARE_TEST} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
      endif()
      set_target_properties(${FIRMWARE_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "FirmwareUpdaterTest")
    foreach(FIRMWARE_TARGET XTEINK_C3 X4PRO X4CLASSIC STICKY MURPHY_M4)
      if(FIRMWARE_TARGET STREQUAL "XTEINK_C3")
        set(FIRMWARE_TEST FirmwareUpdaterTest)
      elseif(FIRMWARE_TARGET STREQUAL "X4PRO")
        set(FIRMWARE_TEST FirmwareUpdaterS3Test)
      elseif(FIRMWARE_TARGET STREQUAL "MURPHY_M4")
        set(FIRMWARE_TEST FirmwareUpdaterMurphyM4Test)
      else()
        set(FIRMWARE_TEST FirmwareUpdater${FIRMWARE_TARGET}Test)
      endif()
      add_executable(${FIRMWARE_TEST}
        ${TEST_SRC}
        ${PROJECT_ROOT}/src/core/FirmwareUpdater.cpp
        ${PROJECT_ROOT}/src/core/FirmwareBoardTag.cpp
        ${PROJECT_ROOT}/src/core/OtaBootSwitch.cpp
        ${TEST_HELPERS}
      )
      target_compile_definitions(${FIRMWARE_TEST} PRIVATE PAPYRIX_TARGET_${FIRMWARE_TARGET}=1)
      if(FIRMWARE_TARGET STREQUAL "MURPHY_M4")
        target_sources(${FIRMWARE_TEST} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
      endif()
      set_target_properties(${FIRMWARE_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
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
  elseif(TEST_NAME STREQUAL "Ssd2677DriverTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/EInkDisplay/src/Ssd2677Driver.cpp ${TEST_HELPERS})
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/EInkDisplay/src)
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_STICKY=1)
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
  elseif(TEST_NAME STREQUAL "StickyUsbTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Usb.cpp
      ${PROJECT_ROOT}/src/hal/Battery.cpp
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/BatteryMonitor.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_STICKY=1)
  elseif(TEST_NAME STREQUAL "FrontLightTest")
    foreach(LIGHT_TARGET X4PRO MURPHY_M4 XTEINK_C3)
      set(LIGHT_TEST FrontLight${LIGHT_TARGET}Test)
      add_executable(${LIGHT_TEST} ${TEST_SRC}
        ${PROJECT_ROOT}/src/hal/FrontLight.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/FrontLightBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${TEST_HELPERS})
      target_compile_definitions(${LIGHT_TEST} PRIVATE PAPYRIX_TARGET_${LIGHT_TARGET}=1)
      if(LIGHT_TARGET STREQUAL "MURPHY_M4")
        target_sources(${LIGHT_TEST} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
      endif()
      set_target_properties(${LIGHT_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    continue()
  elseif(TEST_NAME STREQUAL "MurphyM4PeripheralTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Battery.cpp
      ${PROJECT_ROOT}/src/hal/Usb.cpp
      ${PROJECT_ROOT}/src/hal/FrontLight.cpp
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/BatteryMonitor.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/FrontLightBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
  elseif(TEST_NAME STREQUAL "StickyPeripheralTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/src/hal/Battery.cpp
      ${PROJECT_ROOT}/lib/BatteryMonitor/src/BatteryMonitor.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/RtcBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_STICKY=1)
  elseif(TEST_NAME STREQUAL "MurphyM4RtcTest")
    add_executable(${TEST_NAME} ${TEST_SRC}
      ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/RtcBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
      ${TEST_HELPERS})
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
    target_sources(${TEST_NAME} PRIVATE ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
  elseif(TEST_NAME STREQUAL "RtcBackendTest")
    foreach(RTC_TEST ${TEST_NAME} RtcBm8563Test RtcClassicTest RtcStickyTest)
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
    target_compile_definitions(RtcStickyTest PRIVATE PAPYRIX_TARGET_STICKY=1)
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
    foreach(STORAGE_TARGET C3 X4Pro X4Classic Sticky)
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
    target_compile_definitions(StorageTransportStickyTest PRIVATE PAPYRIX_TARGET_STICKY=1)
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
  elseif(TEST_NAME STREQUAL "MurphyM4BatchSettingsTest")
    foreach(BATCH_UI_TARGET MurphyM4BatchSettingsTest MurphyM4BatchSettingsNonM4Test)
      add_executable(${BATCH_UI_TARGET} ${TEST_SRC}
        ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp
        ${PROJECT_ROOT}/src/states/SettingsState.cpp
        ${PROJECT_ROOT}/src/core/PapyrixSettings.cpp
        ${PROJECT_ROOT}/src/core/SettingsSerialization.cpp
        ${PROJECT_ROOT}/src/hal/Storage.cpp
        ${PROJECT_ROOT}/src/ui/views/SettingsViews.cpp
        ${PROJECT_ROOT}/src/ui/Elements.cpp
        ${PROJECT_ROOT}/src/ui/WrappedText.cpp
        ${PROJECT_ROOT}/src/ui/TouchLayout.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/TouchBackend.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/X4ProBoard.cpp
        ${PROJECT_ROOT}/lib/I18n/src/I18n.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/GfxRenderer.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/Bitmap.cpp
        ${PROJECT_ROOT}/lib/GfxRenderer/src/BitmapHelpers.cpp
        ${PROJECT_ROOT}/lib/EpdFont/src/EpdFont.cpp
        ${PROJECT_ROOT}/lib/EpdFont/src/EpdFontFamily.cpp
        ${PROJECT_ROOT}/lib/EpdFont/src/StreamingEpdFont.cpp
        ${PROJECT_ROOT}/lib/ExternalFont/src/ExternalFont.cpp
        ${PROJECT_ROOT}/lib/ScriptDetector/src/ScriptDetector.cpp
        ${PROJECT_ROOT}/lib/Utf8/src/Utf8.cpp
        ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiClusterBuilder.cpp
        ${PROJECT_ROOT}/lib/ThaiShaper/src/ThaiCharacter.cpp
        ${PROJECT_ROOT}/lib/ArabicShaper/src/ArabicShaper.cpp
        ${PROJECT_ROOT}/lib/ArabicShaper/src/ArabicCharacter.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/HardwareIdentity.cpp
        ${PROJECT_ROOT}/lib/BoardSupport/src/BoardProfiles.cpp
        ${TEST_HELPERS})
      target_compile_definitions(${BATCH_UI_TARGET} PRIVATE PAPYRIX_VERSION="host-test")
      target_include_directories(${BATCH_UI_TARGET} BEFORE PRIVATE
        ${PROJECT_ROOT}/lib/GfxRenderer/src ${PROJECT_ROOT}/lib/EpdFont/src
        ${PROJECT_ROOT}/lib/ExternalFont/src ${PROJECT_ROOT}/lib/ThaiShaper/src)
      target_include_directories(${BATCH_UI_TARGET} PRIVATE
        $<TARGET_PROPERTY:ContentHandleThumbnailTest,INCLUDE_DIRECTORIES>
        ${PROJECT_ROOT}/lib/InputManager/include ${PROJECT_ROOT}/lib/I18n/src)
      target_compile_options(${BATCH_UI_TARGET} PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(${BATCH_UI_TARGET} PRIVATE -Wl,--gc-sections)
      set_target_properties(${BATCH_UI_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
    target_compile_definitions(MurphyM4BatchSettingsTest PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
    target_sources(MurphyM4BatchSettingsTest PRIVATE
      ${PROJECT_ROOT}/lib/BoardSupport/src/Ft6336uBackend.cpp
      ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4I2c.cpp
      ${PROJECT_ROOT}/lib/AsyncTask/src/BackgroundTask.cpp)
    target_compile_definitions(MurphyM4BatchSettingsNonM4Test PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
    continue()
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
  if(TEST_NAME MATCHES "^(ContentHandleThumbnailTest|EpubIndexAnchorsTest|EpubTocRecoveryTest|ChapterHtmlImageSuspensionTest|ChapterAnchorPlacementTest|PngOneBitTest|JpegThumbnailCancellationTest|BmpConversionTest|ImageConverterSignatureTest|ImageFileViewTest)$")
    target_compile_definitions(${TEST_NAME} PRIVATE PAPYRIX_TARGET_XTEINK_C3=1)
  endif()

  target_include_directories(${TEST_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  set_target_properties(${TEST_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
  if(TEST_NAME MATCHES "^(ParserScratch|StreamingEpdFontS3|BitmapTurnCache)Test$")
    string(REPLACE "S3Test" "Test" MEMORY_TEST ${TEST_NAME})
    string(REGEX REPLACE "Test$" "StickyTest" STICKY_TEST ${MEMORY_TEST})
    get_target_property(MEMORY_SOURCES ${TEST_NAME} SOURCES)
    add_executable(${STICKY_TEST} ${MEMORY_SOURCES})
    target_compile_definitions(${STICKY_TEST} PRIVATE PAPYRIX_TARGET_STICKY=1)
    target_include_directories(${STICKY_TEST} BEFORE PRIVATE $<TARGET_PROPERTY:${TEST_NAME},INCLUDE_DIRECTORIES>)
    target_compile_options(${STICKY_TEST} PRIVATE $<TARGET_PROPERTY:${TEST_NAME},COMPILE_OPTIONS>)
    target_link_options(${STICKY_TEST} PRIVATE $<TARGET_PROPERTY:${TEST_NAME},LINK_OPTIONS>)
    set_target_properties(${STICKY_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
    string(REGEX REPLACE "Test$" "MurphyM4Test" M4_MEMORY_TEST ${MEMORY_TEST})
    add_executable(${M4_MEMORY_TEST} ${MEMORY_SOURCES}
      ${PROJECT_ROOT}/lib/BoardSupport/src/MurphyM4Batch.cpp)
    target_compile_definitions(${M4_MEMORY_TEST} PRIVATE PAPYRIX_TARGET_MURPHY_M4=1)
    target_include_directories(${M4_MEMORY_TEST} BEFORE PRIVATE $<TARGET_PROPERTY:${TEST_NAME},INCLUDE_DIRECTORIES>)
    target_compile_options(${M4_MEMORY_TEST} PRIVATE $<TARGET_PROPERTY:${TEST_NAME},COMPILE_OPTIONS>)
    target_link_options(${M4_MEMORY_TEST} PRIVATE $<TARGET_PROPERTY:${TEST_NAME},LINK_OPTIONS>)
    set_target_properties(${M4_MEMORY_TEST} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)
  endif()
endforeach()

message(STATUS "Configured ${TEST_SOURCES} tests")
