#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

// Very low first-order high-pass filter: y[n] = x[n] - x[n-1] + R * y[n-1]
// Removes the DC offset produced by the asymmetric saturation.
class DcBlocker
{
public:
    void prepare (double sampleRate, float cutoffHz)
    {
        r = (float) std::exp (-juce::MathConstants<double>::twoPi * cutoffHz / sampleRate);
        reset();
    }

    void reset() noexcept
    {
        x1.fill (0.0f);
        y1.fill (0.0f);
    }

    float processSample (float x, size_t channel) noexcept
    {
        const auto y = x - x1[channel] + r * y1[channel];
        x1[channel]  = x;
        y1[channel]  = y;
        return y;
    }

private:
    float r = 0.999f;
    std::array<float, 2> x1 {}, y1 {};
};
