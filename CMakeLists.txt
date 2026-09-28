cmake_minimum_required(VERSION 3.22)

# macOS: universal binary, macOS 10.13+ (must be set before project())
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")   # NAM core needs std::filesystem
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(Swarmness VERSION 3.1.0 LANGUAGES C CXX)

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
# Neural Amp Modeler core (MIT) + Eigen (MPL2): the AMP block's NAM slot.
# Built as its own C++20 library behind a small interface (Source/Amp/NamModel.h),
# so the rest of the project stays C++17. -DNAM_CORE_PATH / -DEIGEN_PATH use local copies.
# ============================================================================
set(SWARMNESS_NAM_COMMIT "0b3d3c97")
set(SWARMNESS_EIGEN_COMMIT "bc3b39870ecb690a623a3f49149a358b95c5781d")

if(NOT DEFINED NAM_CORE_PATH AND DEFINED ENV{NAM_CORE_PATH})
    set(NAM_CORE_PATH $ENV{NAM_CORE_PATH})
endif()
if(NOT DEFINED EIGEN_PATH AND DEFINED ENV{EIGEN_PATH})
    set(EIGEN_PATH $ENV{EIGEN_PATH})
endif()

include(FetchContent)
if(NOT DEFINED NAM_CORE_PATH)
    # (SOURCE_SUBDIR without a CMakeLists.txt: only download, the sources are built below)
    FetchContent_Declare(namcore
        GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
        GIT_TAG ${SWARMNESS_NAM_COMMIT}
        GIT_SUBMODULES ""
        SOURCE_SUBDIR swarmness-no-build)
    FetchContent_MakeAvailable(namcore)
    set(NAM_CORE_PATH ${namcore_SOURCE_DIR})
endif()
if(NOT DEFINED EIGEN_PATH)
    FetchContent_Declare(eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG ${SWARMNESS_EIGEN_COMMIT}
        SOURCE_SUBDIR swarmness-no-build)
    FetchContent_MakeAvailable(eigen)
    set(EIGEN_PATH ${eigen_SOURCE_DIR})
endif()
file(TO_CMAKE_PATH "${NAM_CORE_PATH}" NAM_CORE_PATH)
file(TO_CMAKE_PATH "${EIGEN_PATH}" EIGEN_PATH)

file(GLOB_RECURSE SWARMNESS_NAM_SOURCES CONFIGURE_DEPENDS "${NAM_CORE_PATH}/NAM/*.cpp")
add_library(swarmness_nam STATIC ${SWARMNESS_NAM_SOURCES} Source/Amp/NamModel.cpp)
set_target_properties(swarmness_nam PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                               POSITION_INDEPENDENT_CODE ON)
target_include_directories(swarmness_nam SYSTEM PRIVATE "${NAM_CORE_PATH}" "${EIGEN_PATH}" "${NAM_CORE_PATH}/Dependencies" "${NAM_CORE_PATH}/Dependencies/nlohmann")
target_include_directories(swarmness_nam PUBLIC Source/Amp)
# float samples; MPL2-only Eigen; the A2 fast path for current TONE3000 captures
target_compile_definitions(swarmness_nam PRIVATE NAM_SAMPLE_FLOAT EIGEN_MPL2_ONLY NAM_ENABLE_A2_FAST NOMINMAX)
if(MSVC)
    target_compile_options(swarmness_nam PRIVATE /bigobj /MP /W0)
else()
    target_compile_options(swarmness_nam PRIVATE -w)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(swarmness_nam PRIVATE stdc++fs)
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
    MICROPHONE_PERMISSION_TEXT "Swarmness Standalone processes your instrument input."
    NEEDS_CURL TRUE)

juce_generate_juce_header(Swarmness)

# optional artwork (Source/Assets/Skin/README.md): whatever PNGs are there are built in
file(GLOB SWARMNESS_SKIN_ASSETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/Skin/*.png)

juce_add_binary_data(SwarmnessBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        ${SWARMNESS_SKIN_ASSETS}
        Source/Assets/header_logo.png
        Source/Assets/emblem.png
        Source/Assets/Fonts/MetalMania-Regular.ttf
        Source/Assets/Fonts/Rajdhani-Medium.ttf
        Source/Assets/Fonts/Rajdhani-Bold.ttf)

set(SWARMNESS_DSP_SOURCES
    Source/Amp/Tone3000.cpp
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/Preset/PresetManager.cpp)

set(SWARMNESS_GUI_SOURCES
    Source/PluginEditor.cpp
    Source/GUI/SwarmLookAndFeel.cpp
    Source/GUI/Controls.cpp
    Source/GUI/ChainStrip.cpp
    Source/GUI/Pages.cpp
    Source/GUI/TunerOverlay.cpp)

target_sources(Swarmness PRIVATE ${SWARMNESS_DSP_SOURCES} ${SWARMNESS_GUI_SOURCES})

target_include_directories(Swarmness PRIVATE Source)

# TONE3000 publishable key (an OAuth client id, not a secret): -DSWARMNESS_TONE3000_KEY=t3k_pub_...
set(SWARMNESS_TONE3000_KEY "" CACHE STRING "TONE3000 publishable API key")
# Swarmness's own publishable key (public by design: it only identifies the app in the OAuth flow)
set(SWARMNESS_TONE3000_DEFAULT_KEY "t3k_pub_B-IROxQkngaGrrOH8wIVo7ms_FvrO6pa")
if(SWARMNESS_TONE3000_KEY STREQUAL "" AND DEFINED ENV{SWARMNESS_TONE3000_KEY})
    set(SWARMNESS_TONE3000_KEY $ENV{SWARMNESS_TONE3000_KEY})
endif()
if(SWARMNESS_TONE3000_KEY STREQUAL "")
    set(SWARMNESS_TONE3000_KEY "${SWARMNESS_TONE3000_DEFAULT_KEY}")
endif()
if(NOT SWARMNESS_TONE3000_KEY MATCHES "^t3k_pub_")
    message(FATAL_ERROR "SWARMNESS_TONE3000_KEY must be a publishable key (t3k_pub_...), never the secret key")
endif()

# HTTPS for TONE3000: native on macOS / Windows, libcurl on Linux
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SWARMNESS_USE_CURL 1)
else()
    set(SWARMNESS_USE_CURL 0)
endif()

target_compile_definitions(Swarmness PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=${SWARMNESS_USE_CURL}
    SWARMNESS_TONE3000_KEY="${SWARMNESS_TONE3000_KEY}"
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    DONT_SET_USING_JUCE_NAMESPACE=1
    _USE_MATH_DEFINES)

target_link_libraries(Swarmness
    PRIVATE
        SwarmnessBinaryData
        swarmness_nam
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
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
        SWARMNESS_NAM_EXAMPLES="${NAM_CORE_PATH}/example_models"
    _USE_MATH_DEFINES)

    target_link_libraries(SwarmnessTests PRIVATE
        SwarmnessBinaryData
        swarmness_nam
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
        juce::juce_recommended_config_flags)

    if(MSVC)
        target_compile_options(SwarmnessTests PRIVATE /bigobj)
    endif()

    # Amp lab: measures NAM captures and our amp / drive circuits the same way (development tool)
    juce_add_console_app(SwarmnessAmpLab PRODUCT_NAME "SwarmnessAmpLab")
    juce_generate_juce_header(SwarmnessAmpLab)
    target_sources(SwarmnessAmpLab PRIVATE Tests/AmpLab.cpp)
    target_include_directories(SwarmnessAmpLab PRIVATE Source)
    target_compile_definitions(SwarmnessAmpLab PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        DONT_SET_USING_JUCE_NAMESPACE=1
        _USE_MATH_DEFINES)
    target_link_libraries(SwarmnessAmpLab PRIVATE
        swarmness_nam
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    enable_testing()
    add_test(NAME SwarmnessDSP COMMAND SwarmnessTests)
endif()
