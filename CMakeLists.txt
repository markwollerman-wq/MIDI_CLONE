cmake_minimum_required(VERSION 3.22)

# Must be set before project(): universal Mac build (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(MidiClone VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically the first time you configure.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# MIDI Clone is built as an instrument that sends MIDI out. That is what lets
# Ableton route its MIDI to another track ("MIDI From: MIDI Clone").
# VST3 only: AU plug-ins can't send MIDI to other tracks in Ableton.
juce_add_plugin(MidiClone
    COMPANY_NAME                "WOLLER"
    BUNDLE_ID                   com.woller.midiclone
    PRODUCT_NAME                "MIDI Clone"
    PLUGIN_MANUFACTURER_CODE    Wolr
    PLUGIN_CODE                 Mcl1
    FORMATS                     VST3
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Generator)

set(MC_SOURCES
    Source/Engine.cpp
    Source/Keys.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(MC_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_sources(MidiClone PRIVATE ${MC_SOURCES})
target_compile_definitions(MidiClone PUBLIC ${MC_DEFINITIONS})

target_link_libraries(MidiClone
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional test tool: loads a MIDI file, generates 10 clones, checks the note
# percentages, runs the MIDI output with a fake Ableton transport and saves a
# screenshot of the window. Not needed for the plugin itself.
option(MC_BUILD_TESTS "Build the MIDI Clone test tool" OFF)
if(MC_BUILD_TESTS)
    juce_add_console_app(MidiCloneTest PRODUCT_NAME "MidiCloneTest")
    target_sources(MidiCloneTest PRIVATE Tools/MidiCloneTest.cpp ${MC_SOURCES})
    target_compile_definitions(MidiCloneTest PRIVATE ${MC_DEFINITIONS} MC_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MidiCloneTest PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
