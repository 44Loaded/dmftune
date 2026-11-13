cmake_minimum_required(VERSION 3.22)
project(DMF_TUNE VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DMF_TUNE
    COMPANY_NAME "DMF LAB"
    BUNDLE_ID "com.dmflab.dmftune"
    PLUGIN_MANUFACTURER_CODE DmfL
    PLUGIN_CODE DmTn
    FORMATS VST3
    PRODUCT_NAME "DMF TUNE"
    DESCRIPTION "Automatic pitch correction"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES "Fx" "Pitch Shift"
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(DMF_TUNE_Assets
    SOURCES assets/logo.png)

juce_generate_juce_header(DMF_TUNE)

target_sources(DMF_TUNE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DMFTuneDSP.h)

target_include_directories(DMF_TUNE PRIVATE Source)

target_compile_definitions(DMF_TUNE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DMF_TUNE PRIVATE
    juce::juce_audio_utils
    DMF_TUNE_Assets
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
