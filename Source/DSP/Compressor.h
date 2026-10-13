#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

// Feed-forward compressor with a peak detector and ballistics in the dB domain.
// Computes a single gain for all channels (stereo-linked).
class Compressor
{
public:
    void prepare (double sampleRate, float attackMs, float releaseMs)
    {
        attackCoeff  = timeToCoeff (attackMs, sampleRate);
        releaseCoeff = timeToCoeff (releaseMs, sampleRate);
        reset();
    }

    void reset() noexcept { gainReductionDb = 0.0f; }

    void setParameters (float thresholdDb, float ratio, float kneeDb, float makeupAmount) noexcept
    {
        threshold = thresholdDb;
        slope     = 1.0f / ratio - 1.0f;
        knee      = kneeDb;
        // Auto makeup: gives back a fraction of the gain reduction at 0 dBFS.
        makeupDb  = -makeupAmount * gainComputer (0.0f);
    }

    // peak = max |sample| across all channels. Returns the linear gain (makeup included).
    float processPeak (float peak) noexcept
    {
        const auto inDb   = juce::Decibels::gainToDecibels (peak, -120.0f);
        const auto target = gainComputer (inDb);
        const auto coeff  = target < gainReductionDb ? attackCoeff : releaseCoeff;

        gainReductionDb = target + coeff * (gainReductionDb - target);
        return juce::Decibels::decibelsToGain (gainReductionDb + makeupDb, -200.0f);
    }

    // Current gain reduction in dB (<= 0), without makeup. Used by the GR meter.
    float getGainReductionDb() const noexcept { return gainReductionDb; }

private:
    static float timeToCoeff (float ms, double sampleRate)
    {
        return (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
    }

    // Static curve with soft knee. Returns the gain reduction in dB (<= 0).
    float gainComputer (float inDb) const noexcept
    {
        const auto over = inDb - threshold;

        if (2.0f * over < -knee)
            return 0.0f;

        if (2.0f * over <= knee)
        {
            const auto x = over + 0.5f * knee;
            return slope * x * x / (2.0f * knee);
        }

        return slope * over;
    }

    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float threshold = 0.0f, slope = 0.0f, knee = 6.0f, makeupDb = 0.0f;
    float gainReductionDb = 0.0f;
};
