#include "PluginEditor.h"
#include "Parameters.h"

HeftAudioProcessorEditor::HeftAudioProcessorEditor (HeftAudioProcessor& p)
    : AudioProcessorEditor (&p),
      fatnessAttachment (p.apvts, ParamIDs::fatness, fatnessKnob),
      colorAttachment (p.apvts, ParamIDs::color, colorKnob),
      outputAttachment (p.apvts, ParamIDs::output, outputKnob),
      clipperAttachment (p.apvts, ParamIDs::clipper, clipperButton),
      levelMatchAttachment (p.apvts, ParamIDs::levelMatch, levelMatchButton)
{
    setupKnob (fatnessKnob, fatnessLabel, "Fatness");
    setupKnob (colorKnob, colorLabel, "Color");
    setupKnob (outputKnob, outputLabel, "Output");

    addAndMakeVisible (clipperButton);
    addAndMakeVisible (levelMatchButton);

    setSize (480, 280);
}

void HeftAudioProcessorEditor::setupKnob (juce::Slider& knob, juce::Label& label, const juce::String& name)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
    addAndMakeVisible (knob);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

void HeftAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Heft", getLocalBounds().removeFromTop (44), juce::Justification::centred);
}

void HeftAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (32);

    auto buttons = area.removeFromBottom (30);
    clipperButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (20, 0));
    levelMatchButton.setBounds (buttons.reduced (20, 0));

    const auto knobWidth = area.getWidth() / 3;

    for (auto [knob, label] : { std::pair { &fatnessKnob, &fatnessLabel },
                                std::pair { &colorKnob, &colorLabel },
                                std::pair { &outputKnob, &outputLabel } })
    {
        auto column = area.removeFromLeft (knobWidth).reduced (6);
        label->setBounds (column.removeFromTop (22));
        knob->setBounds (column);
    }
}
