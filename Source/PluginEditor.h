#pragma once

#include "PluginProcessor.h"
#include "UI/CurveDisplay.h"
#include "UI/HeftLookAndFeel.h"
#include "UI/Meters.h"

class HeftAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HeftAudioProcessorEditor (HeftAudioProcessor&);
    ~HeftAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Layout is designed at this size and scaled when the window is resized.
    static constexpr int baseWidth = 760, baseHeight = 440;

    void timerCallback() override;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name, const juce::String& paramId,
                    const juce::String& tooltip);
    void layoutKnob (juce::Slider&, juce::Label&, juce::Rectangle<int> area, float scale, bool large);

    HeftAudioProcessor& processor;

    // The look and feel must outlive every component that uses it, so it is declared first.
    HeftLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 700 };

    juce::Slider fatnessKnob, colorKnob, mixKnob, outputKnob;
    juce::Label fatnessLabel, colorLabel, mixLabel, outputLabel;
    juce::ToggleButton clipperButton { "Clipper" }, levelMatchButton { "Level Match" };
    juce::Label levelMatchReadout;

    CurveDisplay curve;
    LevelMeter inputMeter { "IN" }, outputMeter { "OUT" };
    GainReductionMeter gainReductionMeter;

    // Attachments come after the controls so they are destroyed before them.
    SliderAttachment fatnessAttachment, colorAttachment, mixAttachment, outputAttachment;
    ButtonAttachment clipperAttachment, levelMatchAttachment;

    std::atomic<float>* fatnessParam    = nullptr;
    std::atomic<float>* colorParam      = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;

    // Meter ballistics (UI side).
    std::array<float, 2> inputDb { -100.0f, -100.0f }, outputDb { -100.0f, -100.0f };
    float reductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeftAudioProcessorEditor)
};
