#pragma once

#include <cmath>

// Soft clipper: linear up to "knee", then bends with tanh towards "ceiling"
// without ever exceeding it. The slope is continuous at the junction.
struct SoftClipper
{
    static float process (float x, float knee, float ceiling) noexcept
    {
        const auto a = std::abs (x);

        if (a <= knee)
            return x;

        const auto range = ceiling - knee;
        const auto y     = knee + range * std::tanh ((a - knee) / range);
        return x < 0.0f ? -y : y;
    }
};
