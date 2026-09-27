cmake_minimum_required(VERSION 3.22)

project(VibeOTT VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is vendored as a submodule. Building it with source groups keeps the
# module files navigable in an IDE instead of collapsing into one blob.
# Default to a release build when the caller has not asked for anything. JUCE
# then puts the artefacts under build/VibeOTT_artefacts/Release/, which is the
# path this project's README and CI workflow both quote.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "Show JUCE module sources in IDE" FORCE)
add_subdirectory(JUCE)

# JUCE ships the VST3 SDK inside a submodule, so a plain `git clone` without
# --recurse-submodules configures cleanly and then fails deep inside the plugin
# build. Catch it here, where the fix is obvious.
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/fplatform.h")
    message(FATAL_ERROR
        "The VST3 SDK is missing from the JUCE submodule.\n"
        "Run:  git submodule update --init --recursive\n"
        "or point CMake at an existing copy with -DJUCE_VST3_SDK_PATH=<path>.")
endif()

#==============================================================================
# The DSP engine.
#
# It is built as a separate static library because the test harness links it
# directly: no JUCE, no audio device, no host. Anything the tests cannot reach
# is a bug waiting to happen, so keeping the engine JUCE-free is deliberate.
add_library(VibeOTTEngine STATIC
    Source/dsp/OttModule.cpp
)

target_include_directories(VibeOTTEngine
    PUBLIC
        Source
)

set_target_properties(VibeOTTEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

#==============================================================================
juce_add_plugin(VibeOTT
    COMPANY_NAME "VibeAudio"
    PLUGIN_MANUFACTURER_CODE VibA
    PLUGIN_CODE Vott
    FORMATS VST3 Standalone
    PRODUCT_NAME "VibeOTT"
    DESCRIPTION "Three-band upward/downward multiband compressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
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
        VibeOTTEngine
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

#==============================================================================
# Offline DSP regression tests.
#
# These are what catch the failures this engine was rewritten to fix: startup
# clicks, zipper noise under automation, hard clipping, noise-floor explosions
# on silence and stereo image collapse. They run in under a second, so CI runs
# them before it builds the plugin.
option(VIBEOTT_BUILD_TESTS "Build the offline DSP test harness" ON)

if (VIBEOTT_BUILD_TESTS)
    enable_testing()

    add_executable(VibeOTTTests Source/tests/OttTests.cpp)
    target_link_libraries(VibeOTTTests PRIVATE VibeOTTEngine)

    target_compile_options(VibeOTTTests PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )

    add_test(NAME VibeOTT.DSP COMMAND VibeOTTTests)
endif()

#==============================================================================
# Headless editor snapshot tool.
#
# Renders the editor to PNG without a host, an audio device or a window server,
# and asserts the layout invariants the previous UI violated (overlapping
# controls, children escaping their panel, the advanced panel not fitting). Off
# by default because it is a development aid, not something to ship.
option(VIBEOTT_BUILD_SNAPSHOTS "Build the headless editor snapshot tool" OFF)

if (VIBEOTT_BUILD_SNAPSHOTS)
    juce_add_console_app(VibeOTTSnapshot PRODUCT_NAME "VibeOTTSnapshot")

    target_sources(VibeOTTSnapshot PRIVATE
        Source/tests/EditorSnapshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    )

    target_compile_definitions(VibeOTTSnapshot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="VibeOTT"
        JucePlugin_IsSynth=0
        JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0
    )

    target_link_libraries(VibeOTTSnapshot PRIVATE
        VibeOTTEngine
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
    )
endif()
