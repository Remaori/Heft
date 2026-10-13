#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    constexpr int   timerHz           = 30;
    constexpr float meterFallDbPerSec = 24.0f;   // how fast the level meters fall back
    constexpr float grReturnDbPerSec  = 30.0f;   // how fast the GR meter returns to 0

    float toDb (float gain) { return juce::Decibels::gainToDecibels (gain, -100.0f); }
}

HeftAudioProcessorEditor::HeftAudioProcessorEditor (HeftAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      fatnessAttachment (p.apvts, ParamIDs::fatness, fatnessKnob),
      colorAttachment (p.apvts, ParamIDs::color, colorKnob),
      outputAttachment (p.apvts, ParamIDs::output, outputKnob),
      clipperAttachment (p.apvts, ParamIDs::clipper, clipperButton),
      levelMatchAttachment (p.apvts, ParamIDs::levelMatch, levelMatchButton)
{
    // Read the saved size first: setResizeLimits() below resizes the editor, and
    // resized() overwrites editorWidth.
    const auto savedWidth = p.editorWidth.load();

    setLookAndFeel (&lookAndFeel);

    setupKnob (fatnessKnob, fatnessLabel, "Fatness", ParamIDs::fatness,
               "Overall amount: more compression and saturation together. 0 % is transparent.");
    setupKnob (colorKnob, colorLabel, "Color", ParamIDs::color,
               "Saturation character: low is dark and warm, high is bright and aggressive (more even harmonics).");
    setupKnob (outputKnob, outputLabel, "Output", ParamIDs::output, "Final output level.");

    fatnessKnob.getProperties().set ("arc", "fatness");
    colorKnob.getProperties().set ("arc", "color");
    outputKnob.getProperties().set ("arc", "neutral");
    outputKnob.getProperties().set ("bipolarFrom", 0.0);

    clipperButton.setTooltip ("Soft clipper at the very end: the output never goes above 0 dBFS.");
    levelMatchButton.setTooltip ("Matches the output loudness to the input for an honest A/B. "
                                 "The number shows the correction being applied.");
    addAndMakeVisible (clipperButton);
    addAndMakeVisible (levelMatchButton);

    levelMatchReadout.setJustificationType (juce::Justification::centredRight);
    levelMatchReadout.setColour (juce::Label::textColourId, palette::accent);
    levelMatchReadout.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (levelMatchReadout);

    curve.setTooltip ("Saturation curve for the current Fatness and Color (shape only).");
    inputMeter.setTooltip ("Input level (peak).");
    gainReductionMeter.setTooltip ("Gain reduction applied by the compressor.");
    outputMeter.setTooltip ("Output level (peak).");

    for (auto* component : { static_cast<juce::Component*> (&curve), static_cast<juce::Component*> (&inputMeter),
                             static_cast<juce::Component*> (&gainReductionMeter), static_cast<juce::Component*> (&outputMeter) })
        addAndMakeVisible (component);

    fatnessParam    = p.apvts.getRawParameterValue (ParamIDs::fatness);
    colorParam      = p.apvts.getRawParameterValue (ParamIDs::color);
    levelMatchParam = p.apvts.getRawParameterValue (ParamIDs::levelMatch);

    // Resizable with a fixed aspect ratio; the last size is saved with the project.
    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    const auto width = savedWidth > 0 ? juce::jlimit (baseWidth * 3 / 4, baseWidth * 2, savedWidth) : baseWidth;
    setSize (width, juce::roundToInt (width * (double) baseHeight / (double) baseWidth));

    timerCallback();
    startTimerHz (timerHz);
}

HeftAudioProcessorEditor::~HeftAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void HeftAudioProcessorEditor::setupKnob (juce::Slider& knob, juce::Label& label, const juce::String& name,
                                          const juce::String& paramId, const juce::String& tooltip)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTooltip (tooltip + " Double-click to reset; click the value to type it.");

    // Double-click (or Alt+click) returns to the default value.
    if (auto* param = processor.apvts.getParameter (paramId))
        knob.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    addAndMakeVisible (knob);

    label.setText (name.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, palette::textDim);
    addAndMakeVisible (label);
}

//==============================================================================
void HeftAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto scale = (float) getWidth() / (float) baseWidth;
    const auto headerHeight = 56.0f * scale;

    g.setGradientFill (juce::ColourGradient (palette::background, 0.0f, 0.0f,
                                             palette::backgroundLo, 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Header: wordmark, subtitle and version.
    const auto header = getLocalBounds().toFloat().removeFromTop (headerHeight).reduced (22.0f * scale, 0.0f);

    const auto wordmark = palette::font (28.0f * scale, true).withExtraKerningFactor (0.16f);
    g.setColour (palette::text);
    g.setFont (wordmark);
    g.drawText ("HEFT", header, juce::Justification::centredLeft, false);

    const auto wordmarkWidth = juce::GlyphArrangement::getStringWidth (wordmark, "HEFT");
    g.setColour (palette::accent);
    g.setFont (palette::caption (11.0f * scale));
    g.drawText ("FATTENER", header.withTrimmedLeft (wordmarkWidth + 10.0f * scale).translated (0.0f, 2.0f * scale),
                juce::Justification::centredLeft, false);

    g.setColour (palette::textDim);
    g.setFont (palette::font (12.0f * scale));
    g.drawText ("v" JucePlugin_VersionString, header, juce::Justification::centredRight, false);

    g.setColour (palette::panelEdge);
    g.fillRect (0.0f, headerHeight - 1.0f, (float) getWidth(), 1.0f);
}

void HeftAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) baseWidth;
    const auto px = [scale] (float value) { return juce::roundToInt (value * scale); };

    processor.editorWidth = getWidth();

    auto content = getLocalBounds().withTrimmedTop (px (56.0f)).reduced (px (22.0f), px (18.0f));

    // Right: IN, GR and OUT meters.
    auto meterArea = content.removeFromRight (px (144.0f));
    const auto meterWidth = meterArea.getWidth() / 3;
    inputMeter.setBounds (meterArea.removeFromLeft (meterWidth).reduced (px (4.0f), 0));
    gainReductionMeter.setBounds (meterArea.removeFromLeft (meterWidth).reduced (px (4.0f), 0));
    outputMeter.setBounds (meterArea.reduced (px (4.0f), 0));
    content.removeFromRight (px (18.0f));

    // Left: saturation curve and the two switches.
    auto left = content.removeFromLeft (px (214.0f));
    auto switches = left.removeFromBottom (px (86.0f));
    curve.setBounds (left.withTrimmedBottom (px (14.0f)));
    clipperButton.setBounds (switches.removeFromTop (px (38.0f)));
    switches.removeFromTop (px (10.0f));
    levelMatchButton.setBounds (switches.removeFromTop (px (38.0f)));
    levelMatchReadout.setBounds (levelMatchButton.getBounds().reduced (px (14.0f), px (8.0f)).removeFromRight (px (80.0f)));
    levelMatchReadout.setFont (palette::font (13.0f * scale, true));
    content.removeFromLeft (px (18.0f));

    // Centre: the big Fatness knob; to its right, Color above Output.
    auto smallKnobs = content.removeFromRight (px (118.0f));
    layoutKnob (colorKnob, colorLabel, smallKnobs.removeFromTop (smallKnobs.getHeight() / 2), scale, false);
    layoutKnob (outputKnob, outputLabel, smallKnobs, scale, false);
    layoutKnob (fatnessKnob, fatnessLabel, content, scale, true);
}

void HeftAudioProcessorEditor::layoutKnob (juce::Slider& knob, juce::Label& label, juce::Rectangle<int> area,
                                           float scale, bool large)
{
    const auto px = [scale] (float value) { return juce::roundToInt (value * scale); };

    const auto labelHeight = px (large ? 22.0f : 18.0f);
    const auto gap         = px (large ? 6.0f : 2.0f);
    const auto boxHeight   = px (large ? 30.0f : 22.0f);
    const auto diameter    = juce::jmin (area.getWidth(), area.getHeight() - labelHeight - gap - boxHeight);

    // Keep caption, knob and value together, centred in the available space.
    auto group = area.withSizeKeepingCentre (area.getWidth(), labelHeight + gap + diameter + boxHeight);

    label.setFont (palette::caption ((large ? 15.0f : 12.0f) * scale));
    label.setBounds (group.removeFromTop (labelHeight));
    group.removeFromTop (gap);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (large ? 110.0f : 84.0f), boxHeight);
    knob.setBounds (group);
}

//==============================================================================
void HeftAudioProcessorEditor::timerCallback()
{
    constexpr auto dt = 1.0f / (float) timerHz;
    auto& meters = processor.meters;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        inputDb[ch]  = juce::jmax (toDb (meters.inputPeak[ch].exchange (0.0f)), inputDb[ch] - meterFallDbPerSec * dt);
        outputDb[ch] = juce::jmax (toDb (meters.outputPeak[ch].exchange (0.0f)), outputDb[ch] - meterFallDbPerSec * dt);
    }

    reductionDb = juce::jmin (0.0f, meters.gainReductionDb.exchange (0.0f), reductionDb + grReturnDbPerSec * dt);

    inputMeter.setLevels (inputDb);
    outputMeter.setLevels (outputDb);
    gainReductionMeter.setReduction (reductionDb);
    curve.setShape (fatnessParam->load() * 0.01f, colorParam->load() * 0.01f);

    juce::String readout;

    if (levelMatchParam->load() >= 0.5f)
    {
        const auto db = meters.levelMatchDb.load();
        readout = std::abs (db) < 0.05f ? juce::String ("0.0 dB")
                                        : (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    levelMatchReadout.setText (readout, juce::dontSendNotification);
}
