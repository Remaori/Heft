#pragma once

#include <array>

#include "Palette.h"

// Horizontal meters: a caption row (name on the left, a readout on the right) above thin
// bars. Sizes are at 100 % scale; the meters scale with the height the editor gives them.
namespace meterLayout
{
    constexpr float captionHeight = 12.0f, gap = 6.0f, barHeight = 5.0f;

    // Draws the caption row and returns the area below it.
    inline juce::Rectangle<float> drawCaption (juce::Graphics& g, juce::Rectangle<float> bounds, float scale,
                                               const juce::String& name, const juce::String& readout,
                                               juce::Colour readoutColour)
    {
        const auto row = bounds.removeFromTop (captionHeight * scale);

        g.setColour (palette::textDim);
        g.setFont (palette::caption (captionHeight * scale));
        g.drawText (name, row, juce::Justification::centredLeft, false);

        g.setColour (readoutColour);
        g.setFont (palette::font (captionHeight * scale, true));
        g.drawText (readout, row, juce::Justification::centredRight, false);

        return bounds.withTrimmedTop (gap * scale);
    }

    // A bar filled from the left to `amount` (0..1), with thin ticks at the given proportions.
    inline void drawBar (juce::Graphics& g, juce::Rectangle<float> bar, float amount, std::initializer_list<float> ticks)
    {
        g.setColour (palette::track);
        g.fillRect (bar);

        if (amount > 0.0f)
        {
            g.setColour (palette::text);
            g.fillRect (bar.withWidth (amount * bar.getWidth()));
        }

        g.setColour (palette::background);

        for (auto tick : ticks)
            g.fillRect (bar.getX() + tick * bar.getWidth(), bar.getY(), 1.0f, bar.getHeight());
    }
}

// Stereo peak meter, -48..+6 dBFS, with ticks at 0 / -6 / -12 / -24 dB. Left channel on top.
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float preferredHeight = meterLayout::captionHeight + 2.0f * (meterLayout::gap + meterLayout::barHeight);

    explicit LevelMeter (juce::String labelText) : label (std::move (labelText)) {}

    void setLevels (std::array<float, 2> newLevelsDb)
    {
        if (newLevelsDb != levelsDb)
        {
            levelsDb = newLevelsDb;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto scale = (float) getHeight() / preferredHeight;
        auto bars = meterLayout::drawCaption (g, getLocalBounds().toFloat(), scale, label, {}, palette::text);

        for (size_t ch = 0; ch < 2; ++ch)
        {
            meterLayout::drawBar (g, bars.removeFromTop (meterLayout::barHeight * scale), proportion (levelsDb[ch]),
                                  { proportion (0.0f), proportion (-6.0f), proportion (-12.0f), proportion (-24.0f) });
            bars.removeFromTop (meterLayout::gap * scale);
        }
    }

private:
    static constexpr float minDb = -48.0f, maxDb = 6.0f;

    static float proportion (float db) { return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); }

    juce::String label;
    std::array<float, 2> levelsDb { -100.0f, -100.0f };
};

// Gain reduction of the compressor (0..-24 dB, growing to the right), with a readout.
class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float preferredHeight = meterLayout::captionHeight + meterLayout::gap + meterLayout::barHeight;

    void setReduction (float newReductionDb)
    {
        if (newReductionDb != reductionDb)
        {
            reductionDb = newReductionDb;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto scale  = (float) getHeight() / preferredHeight;
        const auto active = reductionDb < -0.05f;
        const auto text = ! active               ? juce::String ("0.0")
                        : reductionDb > -9.95f ? juce::String (reductionDb, 1)
                                               : juce::String (juce::roundToInt (reductionDb));

        auto bar = meterLayout::drawCaption (g, getLocalBounds().toFloat(), scale, "GR", text,
                                             active ? palette::text : palette::textDim);

        meterLayout::drawBar (g, bar.removeFromTop (meterLayout::barHeight * scale),
                              juce::jlimit (0.0f, 1.0f, -reductionDb / rangeDb),
                              { 3.0f / rangeDb, 6.0f / rangeDb, 12.0f / rangeDb });
    }

private:
    static constexpr float rangeDb = 24.0f;
    float reductionDb = 0.0f;
};
