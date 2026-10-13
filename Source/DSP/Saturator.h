#pragma once

#include <cmath>

// Biased tanh saturation, to generate even harmonics.
//
//     y = gain * (tanh(drive * x + bias) - tanh(bias)) / (drive * (1 - tanh(bias)^2))
//
// - Subtracting tanh(bias) keeps y = 0 when x = 0.
// - The divisor keeps the small-signal gain at 1 and lowers the ceiling to ~1/drive,
//   so more drive = more saturation. "gain" restores part of the level.
// - As drive -> 0 the curve tends to y = x (transparent).
struct Saturator
{
    struct Shape
    {
        float drive = 0.0f, bias = 0.0f, biasTanh = 0.0f, norm = 1.0f, gain = 1.0f;
        bool  bypass = true;
    };

    static Shape makeShape (float drive, float bias, float gain) noexcept
    {
        Shape s;
        s.gain   = gain;
        s.bypass = drive < 1.0e-3f;   // below this, the curve is linear for all practical purposes

        if (! s.bypass)
        {
            s.drive    = drive;
            s.bias     = bias;
            s.biasTanh = std::tanh (bias);
            s.norm     = gain / (drive * (1.0f - s.biasTanh * s.biasTanh));
        }

        return s;
    }

    static float process (float x, const Shape& s) noexcept
    {
        if (s.bypass)
            return x * s.gain;

        return (std::tanh (s.drive * x + s.bias) - s.biasTanh) * s.norm;
    }
};
