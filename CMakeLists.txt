cmake_minimum_required(VERSION 3.22)
project(Swarmness VERSION 2.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE setup
if(NOT DEFINED JUCE_PATH)
    set(JUCE_PATH "/home/ubuntu/JUCE")
endif()
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)

# macOS specific
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

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

target_sources(Swarmness
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DSP/GranularPitchShifter.cpp
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

target_compile_definitions(Swarmness
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Swarmness
    PRIVATE
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
