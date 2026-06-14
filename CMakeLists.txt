cmake_minimum_required(VERSION 3.22)
project(VibeOTT VERSION 1.0.0)

set(JUCE_PATH "" CACHE PATH "Path to JUCE root directory (containing CMakeLists.txt)")
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "Show JUCE module sources in IDE")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.9
        SOURCE_DIR ${CMAKE_BINARY_DIR}/JUCE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(VibeOTT
    COMPANY_NAME "VibeAudio"
    PLUGIN_MANUFACTURER_CODE VibA
    PLUGIN_CODE Vott
    FORMATS VST3
    PRODUCT_NAME "VibeOTT"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

target_sources(VibeOTT
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(VibeOTT
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(VibeOTT
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
