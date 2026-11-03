cmake_minimum_required(VERSION 3.22)
project(FretForgeStudio VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FF_BUILD_TESTS  "Build the unit test runner" ON)
option(FF_BUILD_APP    "Build the GUI application" ON)
option(FF_ENABLE_ASIO  "Enable ASIO on Windows (requires the Steinberg ASIO SDK)" OFF)
set(FF_ASIO_SDK_DIR "" CACHE PATH "Path to the ASIO SDK root (contains common/iasiodrv.h)")
set(FF_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE" CACHE PATH "Local JUCE checkout (fetched automatically if missing)")

# ---------------------------------------------------------------- JUCE
if(EXISTS "${FF_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${FF_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(FF_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    FF_VERSION_STRING="${PROJECT_VERSION}")

# JACK/ALSA only on Linux (newer JUCE can also build JACK on macOS/Windows, which needs the JACK SDK)
if(UNIX AND NOT APPLE)
    # libcurl gives juce::URL https on Linux (TONE3000 browser); macOS/Windows use the system networking
    find_package(CURL REQUIRED)
    list(APPEND FF_COMMON_DEFS JUCE_JACK=1 JUCE_ALSA=1 JUCE_USE_CURL=1)
else()
    list(APPEND FF_COMMON_DEFS JUCE_JACK=0)
endif()

if(FF_ENABLE_ASIO AND WIN32)
    if(NOT EXISTS "${FF_ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR "FF_ENABLE_ASIO=ON but FF_ASIO_SDK_DIR does not point to the ASIO SDK")
    endif()
    list(APPEND FF_COMMON_DEFS JUCE_ASIO=1)
endif()

# ---------------------------------------------------------------- Neural Amp Modeler core (vendored, MIT)
file(GLOB FF_NAM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nam/NAM/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nam/NAM/wavenet/*.cpp")
add_library(ff_nam STATIC ${FF_NAM_SOURCES})
target_include_directories(ff_nam SYSTEM PUBLIC
    third_party/nam
    third_party/nam/nlohmann
    third_party/eigen)
target_compile_definitions(ff_nam PUBLIC NAM_SAMPLE_FLOAT EIGEN_MPL2_ONLY PRIVATE NAM_ENABLE_A2_FAST)
if(WIN32)
    target_compile_definitions(ff_nam PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(ff_nam PRIVATE /W0 /bigobj /O2)
else()
    target_compile_options(ff_nam PRIVATE -w -O2)
endif()
set_target_properties(ff_nam PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- core + engine (static, UI-free)
add_library(ff_core STATIC)
target_sources(ff_core PRIVATE
    src/core/MusicTheory.cpp
    src/core/PitchDetector.cpp
    src/core/MetronomeDsp.cpp
    src/core/ProjectFile.cpp
    src/core/Timeline.cpp
    src/core/TabModel.cpp
    src/core/GuitarPro.cpp
    src/core/MusicXml.cpp
    src/core/DrumModel.cpp
    src/core/RigModel.cpp
    src/engine/fx/NamModel.cpp
    src/engine/fx/AmpRig.cpp
    src/engine/fx/RigBank.cpp
    src/engine/TabSynth.cpp
    src/engine/DrumSynth.cpp
    src/engine/AudioCache.cpp
    src/engine/Recorder.cpp
    src/engine/PlaybackGraph.cpp
    src/engine/Exporter.cpp
    src/engine/AudioEngine.cpp
    src/engine/TunerAnalyzer.cpp)
target_include_directories(ff_core PUBLIC src)
target_compile_definitions(ff_core PUBLIC ${FF_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1)
if(FF_ENABLE_ASIO AND WIN32)
    target_include_directories(ff_core PUBLIC "${FF_ASIO_SDK_DIR}/common")
endif()
target_link_libraries(ff_core
    PUBLIC
        ff_nam
        $<$<AND:$<PLATFORM_ID:Linux>,$<BOOL:${CURL_FOUND}>>:CURL::libcurl>
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
set_target_properties(ff_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- GUI application
if(FF_BUILD_APP)
juce_add_gui_app(FretForgeStudio
    PRODUCT_NAME "FretForge Studio"
    COMPANY_NAME "FretForge"
    BUNDLE_ID "com.fretforge.studio"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "FretForge Studio needs audio input to tune and record your instrument."
    VERSION ${PROJECT_VERSION})

target_sources(FretForgeStudio PRIVATE
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/StudioLookAndFeel.cpp
    src/app/ProjectDocument.cpp
    src/app/EngineHost.cpp
    src/app/TransportBar.cpp
    src/app/SessionController.cpp
    src/app/ExportDialog.cpp
    src/app/pages/arrange/ArrangeView.cpp
    src/app/pages/tabs/TabEditor.cpp
    src/app/pages/tabs/NotationGlyphs.cpp
    src/app/pages/tabs/ScorePdf.cpp
    src/app/pages/drums/DrumPage.cpp
    src/app/pages/fx/RigEditor.cpp
    src/app/tone3000/Tone3000.cpp
    src/app/tone3000/Tone3000Panel.cpp
    src/app/pages/tabs/TabPage.cpp
    src/app/pages/StudioPage.cpp
    src/app/pages/TunerPanel.cpp
    src/app/pages/MetronomePanel.cpp
    src/app/pages/MixerPage.cpp
    src/app/pages/SettingsPage.cpp
    src/app/pages/MilestonePage.cpp)

target_link_libraries(FretForgeStudio PRIVATE
    ff_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- tests
if(FF_BUILD_TESTS)
    juce_add_console_app(ff_tests PRODUCT_NAME "ff_tests")
    target_sources(ff_tests PRIVATE
        tests/TestMain.cpp
        tests/MusicTheoryTests.cpp
        tests/PitchDetectorTests.cpp
        tests/MetronomeTests.cpp
        tests/ProjectFileTests.cpp
        tests/AudioEngineTests.cpp
        tests/TimelineTests.cpp
        tests/ArrangementEngineTests.cpp
        tests/TabTests.cpp
        tests/GuitarProTests.cpp
        tests/MusicXmlTests.cpp
        tests/DrumTests.cpp
        tests/AmpRigTests.cpp
        tests/PerfProbe.cpp)
    target_link_libraries(ff_tests PRIVATE ff_core juce::juce_recommended_config_flags)
    target_compile_definitions(ff_tests PRIVATE FF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    enable_testing()
    add_test(NAME unit COMMAND ff_tests)
endif()

# ---------------------------------------------------------------- packaging
if(FF_BUILD_APP)
    install(TARGETS FretForgeStudio BUNDLE DESTINATION . RUNTIME DESTINATION bin)
endif()
set(CPACK_PACKAGE_NAME "FretForgeStudio")
set(CPACK_PACKAGE_VENDOR "FretForge")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "FretForge")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libasound2t64 | libasound2, libfreetype6, libfontconfig1, libx11-6")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libjack-jackd2-0 | pipewire-jack")
endif()
include(CPack)
