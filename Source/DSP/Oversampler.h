#pragma once

#include <cmath>
#include <vector>
#include <juce_dsp/juce_dsp.h>

// 2^n-times oversampling that is bit-identical to juce::dsp::Oversampling<float> with
// half-band FIR equiripple filters, maximum quality and integer latency: same filter
// designs, same stage layout, same fractional delay, same order of every operation.
//
// The one difference is the filter history. For every sample, JUCE shifts the whole
// history of each filter by two places; here the history is a ring with a mirrored copy,
// so the latest values are always contiguous and nothing moves. That halves the cost of
// each up/down pass, and these filters are over 80 % of Heft's CPU.
class Oversampler
{
public:
    Oversampler (size_t channels, int factorLog2) : numChannels (channels)
    {
        for (int n = 0; n < factorLog2; ++n)
        {
            // juce::dsp::Oversampling's settings for maximum quality.
            const auto twUp   = 0.10f * (n == 0 ? 0.5f : 1.0f);
            const auto twDown = 0.12f * (n == 0 ? 0.5f : 1.0f);
            stages.emplace_back (channels,
                                 *juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (twUp, -90.0f + 10.0f * (float) n),
                                 *juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (twDown, -75.0f + 10.0f * (float) n));
        }

        // The filters' latency, then a fractional delay that rounds it up to whole samples,
        // computed exactly as JUCE does.
        auto latency = 0.0f;
        size_t order = 1;

        for (const auto& stage : stages)
        {
            order *= 2;
            latency += stage.latency / (float) order;
        }

        fractionalDelay = 1.0f - (latency - std::floor (latency));

        if (juce::approximatelyEqual (fractionalDelay, 1.0f))
            fractionalDelay = 0.0f;
        else if (fractionalDelay < 0.618f)
            fractionalDelay += 1.0f;

        latencyInSamples = latency + fractionalDelay;
    }

    // Allocates; call it from prepareToPlay, never from the audio thread.
    void initProcessing (size_t maxSamplesBeforeOversampling)
    {
        auto numSamples = maxSamplesBeforeOversampling;

        for (auto& stage : stages)
        {
            numSamples *= 2;
            stage.buffer.setSize ((int) numChannels, (int) numSamples, false, false, true);
        }

        delay.prepare ({ 0.0, (juce::uint32) maxSamplesBeforeOversampling, (juce::uint32) numChannels });
        delay.setDelay (fractionalDelay);
        reset();
    }

    void reset() noexcept
    {
        for (auto& stage : stages)
            stage.reset();

        delay.reset();
    }

    float getLatencyInSamples() const noexcept { return latencyInSamples; }

    juce::dsp::AudioBlock<float> processSamplesUp (const juce::dsp::AudioBlock<const float>& input) noexcept
    {
        const auto channels = input.getNumChannels();
        auto numSamples = input.getNumSamples();

        for (size_t ch = 0; ch < channels; ++ch)
            stages[0].up (input.getChannelPointer (ch), stages[0].buffer.getWritePointer ((int) ch), numSamples, ch);

        for (size_t n = 1; n < stages.size(); ++n)
        {
            numSamples *= 2;

            for (size_t ch = 0; ch < channels; ++ch)
                stages[n].up (stages[n - 1].buffer.getReadPointer ((int) ch), stages[n].buffer.getWritePointer ((int) ch), numSamples, ch);
        }

        return juce::dsp::AudioBlock<float> (stages.back().buffer).getSubsetChannelBlock (0, channels)
                                                                   .getSubBlock (0, numSamples * 2);
    }

    // Reads the block returned by processSamplesUp (processed in place) and writes "output".
    void processSamplesDown (juce::dsp::AudioBlock<float>& output) noexcept
    {
        const auto channels = output.getNumChannels();
        const auto numSamples = output.getNumSamples();

        for (size_t n = stages.size() - 1; n > 0; --n)
            for (size_t ch = 0; ch < channels; ++ch)
                stages[n].down (stages[n].buffer.getReadPointer ((int) ch), stages[n - 1].buffer.getWritePointer ((int) ch),
                                numSamples << n, ch);

        for (size_t ch = 0; ch < channels; ++ch)
            stages[0].down (stages[0].buffer.getReadPointer ((int) ch), output.getChannelPointer (ch), numSamples, ch);

        if (fractionalDelay > 0.0f)
        {
            juce::dsp::ProcessContextReplacing<float> context (output);
            delay.process (context);
        }
    }

private:
    // One 2x stage: an up and a down half-band filter, and the buffer at its output rate.
    //
    // JUCE keeps N values per filter, writes the new one at N - 1 and only ever reads even
    // positions; those (N + 1) / 2 values are the "history" here, oldest first, so JUCE's
    // buf[2j] is history[j]. Every filter here has N = 4k + 3 taps, so the centre tap of the
    // up filter also falls on an even position.
    struct Stage
    {
        Stage (size_t channels, const juce::dsp::FIR::Coefficients<float>& upCoefficients,
               const juce::dsp::FIR::Coefficients<float>& downCoefficients)
            : upFir (upCoefficients.getRawCoefficients(), upCoefficients.getRawCoefficients() + upCoefficients.getFilterOrder() + 1),
              downFir (downCoefficients.getRawCoefficients(), downCoefficients.getRawCoefficients() + downCoefficients.getFilterOrder() + 1),
              latency ((float) (upCoefficients.getFilterOrder() + downCoefficients.getFilterOrder()) * 0.5f),
              upPos (channels, 0), downPos (channels, 0), centrePos (channels, 0)
        {
            jassert (upFir.size() % 4 == 3 && downFir.size() % 4 == 3);
            upHistory.setSize ((int) channels, (int) (2 * historyLength (upFir)));
            downHistory.setSize ((int) channels, (int) (2 * historyLength (downFir)));
            centre.setSize ((int) channels, (int) (downFir.size() / 4 + 1));
        }

        static size_t historyLength (const std::vector<float>& fir) { return (fir.size() + 1) / 2; }

        void reset() noexcept
        {
            buffer.clear();
            upHistory.clear();
            downHistory.clear();
            centre.clear();
            std::fill (upPos.begin(), upPos.end(), (size_t) 0);
            std::fill (downPos.begin(), downPos.end(), (size_t) 0);
            std::fill (centrePos.begin(), centrePos.end(), (size_t) 0);
        }

        // Stores x twice, "length" apart; returns the latest "length" values, oldest first.
        static const float* push (float* ring, size_t& pos, size_t length, float x) noexcept
        {
            pos = (pos + 1 == length) ? 0 : pos + 1;
            ring[pos] = ring[pos + length] = x;
            return ring + pos + 1;
        }

        void up (const float* in, float* out, size_t numSamples, size_t ch) noexcept
        {
            const auto* fir = upFir.data();
            const auto half = upFir.size() / 2;
            const auto length = historyLength (upFir);
            auto* ring = upHistory.getWritePointer ((int) ch);
            auto& pos = upPos[ch];

            for (size_t i = 0; i < numSamples; ++i)
            {
                const auto* h = push (ring, pos, length, 2 * in[i]);
                auto acc = 0.0f;

                for (size_t k = 0, j = 0; k < half; k += 2, ++j)
                    acc += (h[j] + h[length - 1 - j]) * fir[k];

                out[i << 1] = acc;
                out[(i << 1) + 1] = h[(half + 1) / 2] * fir[half];
            }
        }

        void down (const float* in, float* out, size_t numSamples, size_t ch) noexcept
        {
            const auto* fir = downFir.data();
            const auto half = downFir.size() / 2;
            const auto length = historyLength (downFir);
            const auto lastCentre = half / 2;
            auto* ring = downHistory.getWritePointer ((int) ch);
            auto* centreValues = centre.getWritePointer ((int) ch);
            auto& pos = downPos[ch];
            auto cpos = centrePos[ch];

            for (size_t i = 0; i < numSamples; ++i)
            {
                const auto* h = push (ring, pos, length, in[i << 1]);
                auto acc = 0.0f;

                for (size_t k = 0, j = 0; k < half; k += 2, ++j)
                    acc += (h[j] + h[length - 1 - j]) * fir[k];

                acc += centreValues[cpos] * fir[half];
                centreValues[cpos] = in[(i << 1) + 1];
                out[i] = acc;
                cpos = (cpos == 0 ? lastCentre : cpos - 1);
            }

            centrePos[ch] = cpos;
        }

        std::vector<float> upFir, downFir;
        float latency;
        juce::AudioBuffer<float> buffer, upHistory, downHistory, centre;
        std::vector<size_t> upPos, downPos, centrePos;
    };

    size_t numChannels;
    std::vector<Stage> stages;
    float fractionalDelay = 0.0f, latencyInSamples = 0.0f;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran> delay { 8 };
};
