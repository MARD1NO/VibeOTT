cmake_minimum_required(VERSION 3.22)
project(VibeOTT VERSION 1.0.0)

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "Show JUCE module sources in IDE")
add_subdirectory(JUCE)

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
