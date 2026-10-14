#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

// ITU-R BS.1770 K-weighting: a high shelf (+4 dB above ~1.7 kHz, models the head)
// followed by a high-pass at ~38 Hz (the "RLB" curve). The coefficients are derived
// for any sample rate from the analog prototypes (the same method libebur128 uses);
// at 48 kHz they match the values printed in the standard.
// Mean square of the output, averaged over 400 ms, is "momentary loudness".
class KWeighting
{
public:
    void prepare (double sampleRate)
    {
        constexpr auto pi = juce::MathConstants<double>::pi;

        {
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const double k  = std::tan (pi * f0 / sampleRate);
            const double vh = std::pow (10.0, gainDb / 20.0);
            const double vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                      2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }

        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k  = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + k / q + k * k;
            highPass = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }

        reset();
    }

    void reset() noexcept
    {
        shelfState    = {};
        highPassState = {};
    }

    float processSample (float x, size_t channel) noexcept
    {
        const auto y = run (highPass, highPassState[channel], run (shelf, shelfState[channel], (double) x));
        return (float) y;
    }

private:
    struct Coefficients { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct State        { double s1 = 0.0, s2 = 0.0; };

    // Transposed direct form II, in double: the 38 Hz high-pass has poles very close
    // to 1 at high sample rates, where float would lose precision.
    static double run (const Coefficients& c, State& s, double x) noexcept
    {
        const auto y = c.b0 * x + s.s1;
        s.s1 = c.b1 * x - c.a1 * y + s.s2;
        s.s2 = c.b2 * x - c.a2 * y;
        return y;
    }

    Coefficients shelf, highPass;
    std::array<State, 2> shelfState {}, highPassState {};
};
