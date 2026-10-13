#pragma once

#include <array>

#include "Palette.h"

// Vertical stereo peak meter, -48..+6 dBFS, with ticks at 0 / -6 / -12 / -24 dB.
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
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
        auto bounds = getLocalBounds().toFloat();
        const auto labelHeight = juce::jmin (18.0f, bounds.getHeight() * 0.08f);

        g.setColour (palette::textDim);
        g.setFont (palette::caption (juce::jmin (labelHeight * 0.75f, bounds.getWidth() * 0.32f)));
        g.drawText (label, bounds.removeFromBottom (labelHeight), juce::Justification::centred, false);
        bounds.removeFromBottom (labelHeight * 0.4f);

        const auto gap  = bounds.getWidth() * 0.12f;
        const auto barW = (bounds.getWidth() - gap) * 0.5f;

        for (size_t ch = 0; ch < 2; ++ch)
        {
            const auto bar = bounds.withWidth (barW).translated ((barW + gap) * (float) ch, 0.0f);
            drawBar (g, bar, levelsDb[ch]);
        }

        // Ticks across both bars.
        g.setColour (palette::background.withAlpha (0.8f));

        for (auto db : { 0.0f, -6.0f, -12.0f, -24.0f })
        {
            const auto y = bounds.getBottom() - proportion (db) * bounds.getHeight();
            g.fillRect (bounds.getX(), y, bounds.getWidth(), 1.0f);
        }
    }

private:
    static constexpr float minDb = -48.0f, maxDb = 6.0f;

    static float proportion (float db) { return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); }

    static void drawBar (juce::Graphics& g, juce::Rectangle<float> bar, float db)
    {
        const auto corner = bar.getWidth() * 0.3f;
        g.setColour (palette::track);
        g.fillRoundedRectangle (bar, corner);

        const auto level = proportion (db);

        if (level <= 0.0f)
            return;

        juce::ColourGradient gradient (palette::meterLow, 0.0f, bar.getBottom(), palette::meterHigh, 0.0f, bar.getY(), false);
        gradient.addColour (proportion (-12.0f), palette::meterLow);
        gradient.addColour (proportion (-3.0f), palette::meterMid);
        gradient.addColour (proportion (0.0f), palette::meterHigh);

        g.setGradientFill (gradient);
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - level * bar.getHeight()), corner);
    }

    juce::String label;
    std::array<float, 2> levelsDb { -100.0f, -100.0f };
};

// Gain reduction of the compressor, drawn from the top down (0..-24 dB), with a readout.
class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
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
        auto bounds = getLocalBounds().toFloat();
        const auto labelHeight = juce::jmin (18.0f, bounds.getHeight() * 0.08f);

        g.setColour (palette::textDim);
        g.setFont (palette::caption (juce::jmin (labelHeight * 0.75f, bounds.getWidth() * 0.32f)));
        g.drawText ("GR", bounds.removeFromBottom (labelHeight), juce::Justification::centred, false);
        bounds.removeFromBottom (labelHeight * 0.4f);

        const auto readout = bounds.removeFromTop (labelHeight);
        g.setColour (reductionDb < -0.05f ? palette::accent : palette::textDim);
        g.setFont (palette::font (juce::jmin (labelHeight * 0.8f, bounds.getWidth() * 0.36f), true));
        const auto text = reductionDb > -0.05f ? juce::String ("0.0")
                        : reductionDb > -9.95f ? juce::String (reductionDb, 1)
                                               : juce::String (juce::roundToInt (reductionDb));
        g.drawText (text, readout, juce::Justification::centred, false);
        bounds.removeFromTop (labelHeight * 0.4f);

        const auto bar = bounds.withSizeKeepingCentre (bounds.getWidth() * 0.44f, bounds.getHeight());
        const auto corner = bar.getWidth() * 0.3f;

        g.setColour (palette::track);
        g.fillRoundedRectangle (bar, corner);

        const auto amount = juce::jlimit (0.0f, 1.0f, -reductionDb / rangeDb);

        if (amount > 0.0f)
        {
            g.setColour (palette::accent);
            g.fillRoundedRectangle (bar.withHeight (amount * bar.getHeight()), corner);
        }

        g.setColour (palette::background.withAlpha (0.8f));

        for (auto db : { 3.0f, 6.0f, 12.0f })
            g.fillRect (bar.getX(), bar.getY() + db / rangeDb * bar.getHeight(), bar.getWidth(), 1.0f);
    }

private:
    static constexpr float rangeDb = 24.0f;
    float reductionDb = 0.0f;
};
