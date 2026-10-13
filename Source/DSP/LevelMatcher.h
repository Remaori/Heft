#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

// Level matcher: measures the RMS of the input (before any processing) and of
// the processed signal (before the output gain), and computes the gain needed
// for both to sound equally loud.
class LevelMatcher
{
public:
    void prepare (double sampleRate, float windowSec, float glideSec, float rangeDb, float gateDb)
    {
        powerCoeff = (float) std::exp (-1.0 / (windowSec * sampleRate));
        glideCoeff = (float) std::exp (-1.0 / (glideSec * sampleRate));
        minGain    = juce::Decibels::decibelsToGain (-rangeDb);
        maxGain    = juce::Decibels::decibelsToGain (rangeDb);
        gatePower  = std::pow (10.0f, gateDb / 10.0f);
        reset();
    }

    void reset() noexcept
    {
        inputPower = processedPower = 0.0f;
        targetGain = gain = 1.0f;
    }

    // Takes the mean power (x^2) of the input and of the processed signal at
    // the same sample. Returns the compensation gain (linear).
    float process (float inputSquared, float processedSquared) noexcept
    {
        inputPower     = inputSquared     + powerCoeff * (inputPower     - inputSquared);
        processedPower = processedSquared + powerCoeff * (processedPower - processedSquared);

        // In silence, keep the last correction so noise is not amplified.
        if (inputPower > gatePower)
            targetGain = juce::jlimit (minGain, maxGain, std::sqrt (inputPower / (processedPower + 1.0e-12f)));

        gain = targetGain + glideCoeff * (gain - targetGain);
        return gain;
    }

private:
    float powerCoeff = 0.0f, glideCoeff = 0.0f;
    float minGain = 0.1f, maxGain = 10.0f, gatePower = 1.0e-6f;
    float inputPower = 0.0f, processedPower = 0.0f;
    float targetGain = 1.0f, gain = 1.0f;
};
