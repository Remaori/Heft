#pragma once

#include "PluginProcessor.h"

class HeftAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit HeftAudioProcessorEditor (HeftAudioProcessor&);
    ~HeftAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);

    juce::Slider fatnessKnob, colorKnob, outputKnob;
    juce::Label fatnessLabel, colorLabel, outputLabel;
    juce::ToggleButton clipperButton { "Clipper" }, levelMatchButton { "Level Match" };

    // Attachments come after the controls so they are destroyed before them.
    SliderAttachment fatnessAttachment, colorAttachment, outputAttachment;
    ButtonAttachment clipperAttachment, levelMatchAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeftAudioProcessorEditor)
};
