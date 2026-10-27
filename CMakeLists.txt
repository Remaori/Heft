cmake_minimum_required(VERSION 3.22)

project(Heft VERSION 0.5.0 LANGUAGES C CXX)

# Link the C++ runtime statically: the .vst3 does not depend on the Visual C++
# DLLs and runs on any 64-bit Windows.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- JUCE, downloaded automatically and pinned to a stable release ---------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- The plugin ---------------------------------------------------------------
juce_add_plugin(Heft
    COMPANY_NAME                "Remaori"
    PLUGIN_MANUFACTURER_CODE    Rnau
    PLUGIN_CODE                 Hft1
    FORMATS                     VST3
    PRODUCT_NAME                "Heft"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Distortion Dynamics
)

target_compile_features(Heft PRIVATE cxx_std_20)

# Shared by the plugin and the unit tests.
set(HEFT_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets/PresetManager.cpp
)

target_sources(Heft PRIVATE ${HEFT_SOURCES})

target_include_directories(Heft PRIVATE Source)

target_compile_definitions(Heft
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Heft never reads or writes audio files: leave the codecs out of the binary
        # (smaller plugin, fewer third-party licences to ship; see THIRD_PARTY_NOTICES.txt).
        JUCE_USE_FLAC=0
        JUCE_USE_OGGVORBIS=0
        JUCE_USE_OPUS=0
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_USE_WINDOWS_MEDIA_FORMAT=0
)

target_link_libraries(Heft
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Factory presets, compiled into the plugin ------------------------------
# Every Presets/Factory/*.heftpreset is embedded in the binary, so the presets show
# up even if only the .vst3 is copied. Name them "01 Name", "02 Name"... to set the
# order (the number isn't shown). With no files, the plugin just has "Default".
file(GLOB HEFT_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.heftpreset")
list(LENGTH HEFT_FACTORY_PRESETS HEFT_FACTORY_PRESET_COUNT)

if(HEFT_FACTORY_PRESETS)
    juce_add_binary_data(HeftFactoryPresets
        HEADER_NAME HeftFactoryData.h
        NAMESPACE   HeftFactoryData
        SOURCES     ${HEFT_FACTORY_PRESETS})

    target_link_libraries(Heft PRIVATE HeftFactoryPresets)
    target_compile_definitions(Heft PRIVATE HEFT_HAS_FACTORY_PRESETS=1)
endif()

# --- Measurement tool (optional, not built by default) ----------------------
#     cmake --build build --config Release --target HeftMeasure
juce_add_console_app(HeftMeasure PRODUCT_NAME "HeftMeasure")
set_target_properties(HeftMeasure PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_compile_features(HeftMeasure PRIVATE cxx_std_20)
target_sources(HeftMeasure PRIVATE tests/Measure.cpp)
target_include_directories(HeftMeasure PRIVATE Source)   # reuses Source/DSP/KWeighting.h

target_compile_definitions(HeftMeasure
    PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(HeftMeasure
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Unit tests (optional, not built by default) ----------------------------
#     cmake --build build --config Release --target HeftTests
# Compiles the plugin's own sources into a console app and tests presets, A/B and
# the saved state directly. Uses a temporary folder, never Documents.
juce_add_console_app(HeftTests PRODUCT_NAME "HeftTests")
set_target_properties(HeftTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_compile_features(HeftTests PRIVATE cxx_std_20)
target_sources(HeftTests PRIVATE tests/Tests.cpp ${HEFT_SOURCES})
target_include_directories(HeftTests PRIVATE Source)

target_compile_definitions(HeftTests
    PRIVATE
        JucePlugin_Name="Heft"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(HeftTests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# The tests check that every factory preset file made it into the binary.
target_compile_definitions(HeftTests PRIVATE HEFT_FACTORY_PRESET_COUNT=${HEFT_FACTORY_PRESET_COUNT})

if(HEFT_FACTORY_PRESETS)
    target_link_libraries(HeftTests PRIVATE HeftFactoryPresets)
    target_compile_definitions(HeftTests PRIVATE HEFT_HAS_FACTORY_PRESETS=1)
endif()
