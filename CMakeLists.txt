cmake_minimum_required(VERSION 3.22)
project(Swarmness VERSION 1.2.8)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# JUCE Setup
# ============================================================================
# JUCE_PATH must be provided by user or set via environment variable
if(NOT DEFINED JUCE_PATH)
    if(DEFINED ENV{JUCE_PATH})
        set(JUCE_PATH $ENV{JUCE_PATH})
    else()
        message(FATAL_ERROR "JUCE_PATH not defined. Set it via -DJUCE_PATH=/path/to/JUCE or JUCE_PATH environment variable")
    endif()
endif()

# Normalize path for Windows compatibility
file(TO_CMAKE_PATH "${JUCE_PATH}" JUCE_PATH)
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)

# ============================================================================
# Platform-Specific Settings
# ============================================================================

# macOS
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

# Windows (MSVC)
if(WIN32)
    # Use static runtime for standalone distribution
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # Suppress MSVC warnings for JUCE
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
    
    # Enable parallel compilation
    add_compile_options(/MP)
    
    # Set Windows version (Windows 10+)
    add_compile_definitions(
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
endif()

# ============================================================================
# Plugin Configuration
# ============================================================================
juce_add_plugin(Swarmness
    JUCE_GENERATE_JUCE_HEADER ON
    COMPANY_NAME "OpenAudio"
    BUNDLE_ID "com.OpenAudio.Swarmness"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE OpAu
    PLUGIN_CODE SwMs
    FORMATS VST3
    VST3_CATEGORIES Fx "Pitch Shift" Distortion Modulation
    PRODUCT_NAME "Swarmness"
)

juce_generate_juce_header(Swarmness)

# ============================================================================
# Binary Data (Assets)
# ============================================================================
juce_add_binary_data(SwarmnessBinaryData
    SOURCES
        Source/Assets/footswitch.png
        Source/Assets/background_clean.png
        Source/Assets/knob.png
        Source/Assets/toggle.png
        Source/Assets/fader.png
        Source/Assets/power_button.png
        Source/Assets/header_logo.png
)

# ============================================================================
# Source Files
# ============================================================================
target_sources(Swarmness
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DSP/PitchSlideEngine.cpp
        Source/DSP/PitchRandomizer.cpp
        Source/DSP/Modulation.cpp
        Source/DSP/ChorusEngine.cpp
        Source/DSP/FlowEngine.cpp
        Source/DSP/DCBlocker.cpp
        Source/DSP/Saturation.cpp
        Source/GUI/MetalLookAndFeel.cpp
        Source/GUI/RotaryKnob.cpp
        Source/GUI/FootswitchButton.cpp
        Source/GUI/PresetPanel.cpp
        Source/Preset/PresetManager.cpp
)

# ============================================================================
# Compile Definitions
# ============================================================================
target_compile_definitions(Swarmness
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ============================================================================
# Link Libraries
# ============================================================================
target_link_libraries(Swarmness
    PRIVATE
        SwarmnessBinaryData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ============================================================================
# Installation (Optional)
# ============================================================================
if(WIN32)
    # Windows VST3 default path
    set(VST3_INSTALL_DIR "$ENV{COMMONPROGRAMFILES}/VST3" CACHE PATH "VST3 install directory")
elseif(APPLE)
    set(VST3_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3" CACHE PATH "VST3 install directory")
else()
    set(VST3_INSTALL_DIR "$ENV{HOME}/.vst3" CACHE PATH "VST3 install directory")
endif()

install(TARGETS Swarmness_VST3
    LIBRARY DESTINATION "${VST3_INSTALL_DIR}"
    RUNTIME DESTINATION "${VST3_INSTALL_DIR}"
)
