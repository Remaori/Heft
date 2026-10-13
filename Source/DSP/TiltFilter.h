#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

// First-order tilt EQ around a pivot frequency:
//     H(s) = g * (s + w/g) / (s + w*g)
// Lows * 1/g, highs * g, pivot at 0 dB. Using 1/g gives the exact inverse, so
// the de-emphasis cancels the pre-emphasis when there is no saturation in between.
class TiltFilter
{
public:
    struct Coefficients
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    };

    void prepare (double sampleRate, float pivotHz)
    {
        // Bilinear transform, pre-warped at the pivot.
        k = (float) (1.0 / std::tan (juce::MathConstants<double>::pi * pivotHz / sampleRate));
        reset();
    }

    void reset() noexcept { state.fill (0.0f); }

    // highGain = g (linear gain of the highs). The lows get 1/g.
    Coefficients makeCoefficients (float highGain) const noexcept
    {
        const auto g    = highGain;
        const auto norm = 1.0f / (k + g);
        return { (g * k + 1.0f) * norm, (1.0f - g * k) * norm, (g - k) * norm };
    }

    // Transposed direct form II, one state per channel.
    float processSample (float x, size_t channel, const Coefficients& c) noexcept
    {
        auto& s      = state[channel];
        const auto y = c.b0 * x + s;
        s            = c.b1 * x - c.a1 * y;
        return y;
    }

private:
    float k = 1.0f;
    std::array<float, 2> state {};
};
