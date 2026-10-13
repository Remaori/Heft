#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Internal parameter IDs. Don't change them once the plugin is used in
// projects: REAPER stores values and automation under these IDs.
namespace ParamIDs
{
    inline constexpr auto fatness    = "fatness";
    inline constexpr auto color      = "color";
    inline constexpr auto output     = "output";
    inline constexpr auto clipper    = "clipper";
    inline constexpr auto levelMatch = "levelMatch";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float value, int) { return String (value, 1) + " %"; });

    auto decibels = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float value, int) { return (value > 0.0f ? "+" : "") + String (value, 1) + " dB"; });

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::fatness, 1 }, "Fatness",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, percent));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::color, 1 }, "Color",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, decibels));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::clipper, 1 }, "Clipper", false));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::levelMatch, 1 }, "Level Match", false));

    return layout;
}
