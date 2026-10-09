cmake_minimum_required(VERSION 3.22)

project(LABX19 VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugin then needs no VC++ redistributable on other machines.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(LABX19_JUCE_PATH "F:/_DEV/tools/JUCE" CACHE PATH "Path to a JUCE 9 checkout")
if(NOT EXISTS "${LABX19_JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${LABX19_JUCE_PATH}. Configure with -DLABX19_JUCE_PATH=<path to JUCE 9>.")
endif()
add_subdirectory("${LABX19_JUCE_PATH}" JUCE)

juce_add_plugin(LabX19
    PRODUCT_NAME "LAB X-19"
    VERSION 0.1.1
    COMPANY_NAME "OPERATION FAIRWAY, LLC"
    COMPANY_WEBSITE "https://github.com/crystalball-dev/LAB-X-19"
    BUNDLE_ID com.operationfairway.labx19
    PLUGIN_MANUFACTURER_CODE Opfw
    PLUGIN_CODE Lb19
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

set(LABX19_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpecimenLibrary.cpp
    Source/FactoryPresets.cpp)

set(LABX19_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_sources(LabX19 PRIVATE ${LABX19_SOURCES})
target_include_directories(LabX19 PRIVATE Source)
target_compile_definitions(LabX19 PUBLIC ${LABX19_DEFINITIONS})
target_compile_options(LabX19 PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(LabX19
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline render and test harness: same sources, driven from the command line.
juce_add_console_app(LabX19Render PRODUCT_NAME "LabX19Render")
target_sources(LabX19Render PRIVATE Tests/RenderHarness.cpp ${LABX19_SOURCES})
target_include_directories(LabX19Render PRIVATE Source)
target_compile_definitions(LabX19Render PRIVATE ${LABX19_DEFINITIONS} LABX19_HARNESS=1)
target_compile_options(LabX19Render PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(LabX19Render
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
