#pragma once

#include "Palette.h"
#include "PluginProcessor.h"

// Draws the saturation transfer curve (input -> output) for the current Fatness
// and Color. Only the shape is shown: the curve is scaled so its largest output
// is 1, which makes the bend (drive) and the asymmetry (even harmonics) visible.
class CurveDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setShape (float newFatness, float newColor)
    {
        if (newFatness == fatness && newColor == color)
            return;

        fatness = newFatness;
        color   = newColor;
        rebuildCurve();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto corner = bounds.getHeight() * 0.05f;

        g.setColour (palette::panel);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (palette::panelEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        g.setColour (palette::textDim);
        g.setFont (palette::caption (titleHeight() * 0.8f));
        g.drawText ("SATURATION", titleArea(), juce::Justification::centredLeft, false);

        // Grid: axes, quarter lines and the straight (clean) reference.
        const auto plot = plotArea();
        g.setColour (palette::panelEdge);

        for (auto f : { 0.25f, 0.75f })
        {
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * f), plot.getX(), plot.getRight());
            g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * f), plot.getY(), plot.getBottom());
        }

        g.setColour (palette::panelEdge.brighter (0.25f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
        g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());

        juce::Path reference, dashed;
        reference.startNewSubPath (plot.getBottomLeft());
        reference.lineTo (plot.getTopRight());
        const float dashes[] = { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, reference, dashes, 2);
        g.setColour (palette::textDim.withAlpha (0.35f));
        g.fillPath (dashed);

        // The curve, with a glow.
        const auto colour = palette::warm.interpolatedWith (palette::bright, color);
        const auto width  = juce::jmax (1.5f, plot.getWidth() * 0.012f);

        g.setColour (colour.withAlpha (0.2f));
        g.strokePath (curve, juce::PathStrokeType (width * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void resized() override { rebuildCurve(); }

private:
    float titleHeight() const
    {
        return juce::jmax (10.0f, juce::jmin ((float) getHeight() * 0.085f, (float) getWidth() * 0.065f));
    }

    juce::Rectangle<float> titleArea() const
    {
        return getLocalBounds().toFloat().reduced (titleHeight() * 0.9f, titleHeight() * 0.5f).removeFromTop (titleHeight());
    }

    juce::Rectangle<float> plotArea() const
    {
        auto area = getLocalBounds().toFloat();
        area.removeFromTop (titleHeight() * 1.6f);
        const auto side = juce::jmin (area.getWidth(), area.getHeight()) - titleHeight() * 1.2f;
        return area.withSizeKeepingCentre (side, side);
    }

    void rebuildCurve()
    {
        curve.clear();
        const auto plot = plotArea();

        if (plot.isEmpty())
            return;

        constexpr int numPoints = 160;
        std::array<float, numPoints> ys {};
        const auto shape = HeftAudioProcessor::saturatorShapeFor (fatness, color);
        float peak = 1.0e-6f;

        for (int i = 0; i < numPoints; ++i)
        {
            const auto x = juce::jmap ((float) i, 0.0f, (float) (numPoints - 1), -1.0f, 1.0f);
            ys[(size_t) i] = Saturator::process (x, shape);
            peak = juce::jmax (peak, std::abs (ys[(size_t) i]));
        }

        for (int i = 0; i < numPoints; ++i)
        {
            const juce::Point<float> p { plot.getX() + plot.getWidth() * (float) i / (float) (numPoints - 1),
                                         plot.getCentreY() - plot.getHeight() * 0.5f * ys[(size_t) i] / peak };

            if (i == 0)
                curve.startNewSubPath (p);
            else
                curve.lineTo (p);
        }
    }

    float fatness = -1.0f, color = -1.0f;
    juce::Path curve;
};
