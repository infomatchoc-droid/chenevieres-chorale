cmake_minimum_required(VERSION 3.22)

project(ChenevieresChorale VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary (Apple Silicon + Intel) on macOS
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# Sources en UTF-8 (accents de l'interface)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# JUCE : copie locale si JUCE_DIR est donné, sinon téléchargement
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

# AAX (Pro Tools) : même principe que Chènevières EQ (signature PACE après le build)
option(CHNV_AAX "Build the AAX plugin (Pro Tools)" ON)
if(CHNV_AAX AND (APPLE OR WIN32))
    list(APPEND PLUGIN_FORMATS AAX)
endif()

option(CHNV_STANDALONE "Build the standalone app too" OFF)
if(CHNV_STANDALONE)
    list(APPEND PLUGIN_FORMATS Standalone)
endif()

juce_add_plugin(ChenevieresChorale
    COMPANY_NAME "Studio des Chenevieres"
    COMPANY_WEBSITE "https://studiodeschenevieres.fr"
    BUNDLE_ID "fr.chenevieres.chorale"
    PLUGIN_MANUFACTURER_CODE Chnv
    PLUGIN_CODE Chor
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Chenevieres Chorale"
    DESCRIPTION "Choeur virtuel"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Pitch Shift Spatial
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    AAX_CATEGORY PitchShift
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(ChenevieresChoraleData SOURCES
    Resources/logo_wordmark.png
    Resources/logo_beagle.png
    Resources/logo_beagle_closed.png
    Resources/Cinzel-Regular.ttf
    Resources/Cinzel-Bold.ttf
    Resources/Cormorant-Italic.ttf)

target_sources(ChenevieresChorale PRIVATE
    Source/ChoirEngine.cpp
    Source/Presets.cpp
    Source/PluginProcessor.cpp
    Source/StageView.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ChenevieresChorale PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ChenevieresChorale
    PRIVATE
        ChenevieresChoraleData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(NOT MSVC)
    target_compile_options(ChenevieresChorale PRIVATE -Wno-float-equal)
endif()

# Tests du moteur (sans JUCE) : ctest
enable_testing()
add_executable(EngineTest tests/EngineTest.cpp Source/ChoirEngine.cpp)
add_test(NAME EngineTest COMMAND EngineTest)

# Capture de l'interface (docs/apercu.png) et test du processeur : -DCHNV_TESTS=ON
option(CHNV_TESTS "Build the processor test and snapshot tool" OFF)
if(CHNV_TESTS)
    add_executable(ChoraleSnapshot tests/Snapshot.cpp)
    target_link_libraries(ChoraleSnapshot PRIVATE ChenevieresChorale)
    target_include_directories(ChoraleSnapshot PRIVATE $<TARGET_PROPERTY:ChenevieresChorale,INCLUDE_DIRECTORIES>)
    target_compile_definitions(ChoraleSnapshot PRIVATE $<TARGET_PROPERTY:ChenevieresChorale,COMPILE_DEFINITIONS>)

    add_executable(ProcessorTest tests/ProcessorTest.cpp)
    target_link_libraries(ProcessorTest PRIVATE ChenevieresChorale)
    target_include_directories(ProcessorTest PRIVATE $<TARGET_PROPERTY:ChenevieresChorale,INCLUDE_DIRECTORIES>)
    target_compile_definitions(ProcessorTest PRIVATE $<TARGET_PROPERTY:ChenevieresChorale,COMPILE_DEFINITIONS>)
    add_test(NAME ProcessorTest COMMAND ProcessorTest)
endif()
