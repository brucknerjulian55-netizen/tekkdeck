cmake_minimum_required(VERSION 3.22)
project(Tekkdeck VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TEKK_JUCE_DIR "" CACHE PATH "Lokaler JUCE-Ordner (leer = automatisch laden)")
if(TEKK_JUCE_DIR)
    add_subdirectory(${TEKK_JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Tekkdeck
    PRODUCT_NAME "Tekkdeck"
    COMPANY_NAME "Tekkbox"
    PLUGIN_MANUFACTURER_CODE Tkbx
    PLUGIN_CODE Tkdk
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Sampler)

target_sources(Tekkdeck PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(Tekkdeck PRIVATE Source)
target_compile_definitions(Tekkdeck PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(MSVC)
    target_compile_options(Tekkdeck PRIVATE /utf-8)
endif()
target_link_libraries(Tekkdeck PRIVATE
    juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
