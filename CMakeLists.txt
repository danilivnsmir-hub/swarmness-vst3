cmake_minimum_required(VERSION 3.22)

# macOS: universal binary, macOS 10.13+ (must be set before project())
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(Swarmness VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
    # Static runtime: the plug-in runs without the VC++ redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(SWARMNESS_BUILD_TESTS "Build the offline DSP test runner" ON)

# ============================================================================
# JUCE: use -DJUCE_PATH=/path/to/JUCE (or the JUCE_PATH env var), otherwise
# JUCE is fetched automatically.
# ============================================================================
set(SWARMNESS_JUCE_TAG "8.0.15")

if(NOT DEFINED JUCE_PATH AND DEFINED ENV{JUCE_PATH})
    set(JUCE_PATH $ENV{JUCE_PATH})
endif()

if(DEFINED JUCE_PATH)
    file(TO_CMAKE_PATH "${JUCE_PATH}" JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${SWARMNESS_JUCE_TAG}
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ============================================================================
# Plug-in
# ============================================================================
set(SWARMNESS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SWARMNESS_FORMATS AU)
endif()

juce_add_plugin(Swarmness
    PRODUCT_NAME "Swarmness"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Insect Audio"
    COMPANY_WEBSITE "https://github.com/danilivnsmir-hub/swarmness-vst3"
    BUNDLE_ID "com.OpenAudio.Swarmness"
    PLUGIN_MANUFACTURER_CODE OpAu
    PLUGIN_CODE SwMs
    FORMATS ${SWARMNESS_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    HARDENED_RUNTIME_ENABLED TRUE
    VST3_CATEGORIES Fx "Pitch Shift" Modulation
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Swarmness Standalone processes your instrument input.")

juce_generate_juce_header(Swarmness)

juce_add_binary_data(SwarmnessBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Source/Assets/header_logo.png
        Source/Assets/emblem.png
        Source/Assets/Fonts/MetalMania-Regular.ttf
        Source/Assets/Fonts/Rajdhani-Medium.ttf
        Source/Assets/Fonts/Rajdhani-Bold.ttf)

set(SWARMNESS_DSP_SOURCES
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/Preset/PresetManager.cpp)

set(SWARMNESS_GUI_SOURCES
    Source/PluginEditor.cpp
    Source/GUI/SwarmLookAndFeel.cpp
    Source/GUI/Controls.cpp
    Source/GUI/ChainStrip.cpp
    Source/GUI/Pages.cpp)

target_sources(Swarmness PRIVATE ${SWARMNESS_DSP_SOURCES} ${SWARMNESS_GUI_SOURCES})

target_include_directories(Swarmness PRIVATE Source)

target_compile_definitions(Swarmness PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    DONT_SET_USING_JUCE_NAMESPACE=1
    _USE_MATH_DEFINES)

target_link_libraries(Swarmness
    PRIVATE
        SwarmnessBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Swarmness PRIVATE /bigobj /MP)
endif()

# ============================================================================
# Offline tests (headless): run with `ctest` after building.
# ============================================================================
if(SWARMNESS_BUILD_TESTS)
    juce_add_console_app(SwarmnessTests PRODUCT_NAME "SwarmnessTests")
    juce_generate_juce_header(SwarmnessTests)

    target_sources(SwarmnessTests PRIVATE
        Tests/SwarmnessTests.cpp
        ${SWARMNESS_DSP_SOURCES}
        ${SWARMNESS_GUI_SOURCES})

    target_include_directories(SwarmnessTests PRIVATE Source)

    target_compile_definitions(SwarmnessTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Swarmness"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        DONT_SET_USING_JUCE_NAMESPACE=1
    _USE_MATH_DEFINES)

    target_link_libraries(SwarmnessTests PRIVATE
        SwarmnessBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    if(MSVC)
        target_compile_options(SwarmnessTests PRIVATE /bigobj)
    endif()

    enable_testing()
    add_test(NAME SwarmnessDSP COMMAND SwarmnessTests)
endif()
