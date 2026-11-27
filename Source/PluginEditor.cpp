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
      mixAttachment (p.apvts, ParamIDs::mix, mixKnob),
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
    setupKnob (mixKnob, mixLabel, "Mix", ParamIDs::mix,
               "Dry/wet: blends the processed signal with the original, time-aligned (parallel processing). "
               "100 % is fully processed.");
    setupKnob (outputKnob, outputLabel, "Output", ParamIDs::output, "Final output level, after the mix.");

    outputKnob.getProperties().set ("bipolarFrom", 0.0);

    clipperButton.setTooltip ("Soft clipper at the very end, oversampled 4x: sample peaks never go above 0 dBFS "
                              "and most inter-sample peaks are caught too.");
    levelMatchButton.setTooltip ("Matches the processed loudness to the input (K-weighted, like LUFS) for an "
                                 "honest A/B. The number shows the correction being applied.");
    addAndMakeVisible (clipperButton);
    addAndMakeVisible (levelMatchButton);

    levelMatchReadout.setJustificationType (juce::Justification::centredRight);
    levelMatchReadout.setColour (juce::Label::textColourId, palette::textDim);
    levelMatchReadout.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (levelMatchReadout);

    inputMeter.setTooltip ("Input level (peak).");
    gainReductionMeter.setTooltip ("Gain reduction applied by the compressor.");
    outputMeter.setTooltip ("Output level (peak).");

    for (auto* component : { static_cast<juce::Component*> (&inputMeter), static_cast<juce::Component*> (&gainReductionMeter),
                             static_cast<juce::Component*> (&outputMeter) })
        addAndMakeVisible (component);

    addAndMakeVisible (presetBar);

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

bool HeftAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (path.endsWithIgnoreCase (PresetFile::extension))
            return true;

    return false;
}

void HeftAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> presets;

    for (const auto& path : files)
        if (path.endsWithIgnoreCase (PresetFile::extension))
            presets.add (juce::File (path));

    presetBar.importFiles (presets);
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

    g.fillAll (palette::background);

    // Header: wordmark and version. The preset bar fills the space between.
    const auto header = getHeader();

    g.setColour (palette::text);
    g.setFont (header.wordmark);
    g.drawText ("HEFT", header.area, juce::Justification::centredLeft, false);

    g.setColour (palette::textDim);
    g.setFont (header.version);
    g.drawText ("v" JucePlugin_VersionString, header.area, juce::Justification::centredRight, false);

    g.setColour (palette::divider);
    g.fillRect (0.0f, headerHeight - 1.0f, (float) getWidth(), 1.0f);
    g.fillRect (columnLine);
}

HeftAudioProcessorEditor::Header HeftAudioProcessorEditor::getHeader() const
{
    const auto scale = (float) getWidth() / (float) baseWidth;

    Header header;
    header.area     = getLocalBounds().toFloat().removeFromTop (56.0f * scale).reduced (20.0f * scale, 0.0f);
    header.wordmark = palette::font (24.0f * scale, true).withExtraKerningFactor (0.32f);
    header.version  = palette::font (13.0f * scale);

    header.freeLeft  = header.area.getX() + juce::GlyphArrangement::getStringWidth (header.wordmark, "HEFT") + 10.0f * scale;
    header.freeRight = header.area.getRight() - juce::GlyphArrangement::getStringWidth (header.version, "v" JucePlugin_VersionString);
    return header;
}

void HeftAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) baseWidth;
    const auto px = [scale] (float value) { return juce::roundToInt (value * scale); };

    processor.editorWidth = getWidth();

    // Preset bar: centred in the header's free space, same height at every scale.
    {
        const auto header = getHeader();
        const auto width  = (float) PresetBar::preferredWidth * scale;
        const auto height = (float) PresetBar::preferredHeight * scale;
        const auto x = juce::jlimit (header.freeLeft, juce::jmax (header.freeLeft, header.freeRight - width),
                                     (header.freeLeft + header.freeRight - width) * 0.5f);
        presetBar.setBounds (juce::Rectangle<float> (x, header.area.getCentreY() - height * 0.5f, width, height).toNearestInt());
    }

    auto content = getLocalBounds().withTrimmedTop (px (56.0f)).reduced (px (24.0f), px (18.0f));

    // Right column, after a thin line: the two switches on top, the meters at the bottom.
    auto right = content.removeFromRight (px (214.0f));
    columnLine = right.removeFromLeft (px (22.0f)).withWidth (1);
    content.removeFromRight (px (24.0f));

    clipperButton.setBounds (right.removeFromTop (px (36.0f)));
    right.removeFromTop (px (8.0f));
    levelMatchButton.setBounds (right.removeFromTop (px (36.0f)));
    levelMatchReadout.setBounds (levelMatchButton.getBounds().reduced (px (8.0f), px (6.0f)).removeFromRight (px (70.0f)));
    levelMatchReadout.setFont (palette::font (14.0f * scale));

    outputMeter.setBounds (right.removeFromBottom (px (LevelMeter::preferredHeight)));
    right.removeFromBottom (px (16.0f));
    gainReductionMeter.setBounds (right.removeFromBottom (px (GainReductionMeter::preferredHeight)));
    right.removeFromBottom (px (16.0f));
    inputMeter.setBounds (right.removeFromBottom (px (LevelMeter::preferredHeight)));

    // Left: the big Fatness knob on top, then Color, Mix and Output in a row at the bottom.
    auto row = content.removeFromBottom (px (100.0f));
    const auto smallWidth = px (84.0f), smallGap = px (32.0f);
    row = row.withSizeKeepingCentre (smallWidth * 3 + smallGap * 2, row.getHeight());
    layoutKnob (colorKnob, colorLabel, row.removeFromLeft (smallWidth), scale, false);
    row.removeFromLeft (smallGap);
    layoutKnob (mixKnob, mixLabel, row.removeFromLeft (smallWidth), scale, false);
    row.removeFromLeft (smallGap);
    layoutKnob (outputKnob, outputLabel, row, scale, false);

    const auto top = content.removeFromTop (px (200.0f));
    layoutKnob (fatnessKnob, fatnessLabel, top.withSizeKeepingCentre (px (170.0f), top.getHeight()), scale, true);
}

void HeftAudioProcessorEditor::layoutKnob (juce::Slider& knob, juce::Label& label, juce::Rectangle<int> area,
                                           float scale, bool large)
{
    const auto px = [scale] (float value) { return juce::roundToInt (value * scale); };

    const auto labelHeight = px (large ? 18.0f : 14.0f);
    const auto gap         = px (large ? 8.0f : 6.0f);
    const auto boxHeight   = px (large ? 26.0f : 22.0f);
    const auto diameter    = juce::jmin (area.getWidth(), area.getHeight() - labelHeight - gap - boxHeight);

    // Keep caption, knob and value together, centred in the available space.
    auto group = area.withSizeKeepingCentre (area.getWidth(), labelHeight + gap + diameter + boxHeight);

    label.setFont (palette::caption ((large ? 14.0f : 12.0f) * scale));
    label.setBounds (group.removeFromTop (labelHeight));
    group.removeFromTop (gap);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (large ? 84.0f : 68.0f), boxHeight);
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

    juce::String readout;

    if (levelMatchParam->load() >= 0.5f)
    {
        const auto db = meters.levelMatchDb.load();
        readout = std::abs (db) < 0.05f ? juce::String ("0.0 dB")
                                        : (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    levelMatchReadout.setText (readout, juce::dontSendNotification);
    presetBar.refresh();
}
