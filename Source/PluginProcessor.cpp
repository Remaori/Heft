#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "Tuning.h"

namespace
{
    // State property that remembers the editor size in the project.
    const juce::Identifier editorWidthId { "editorWidth" };

    // Meter accumulation (audio thread): keep the most extreme value until the editor
    // reads it. A rare race with the editor's exchange() only delays a peak by one frame.
    void storeMax (std::atomic<float>& target, float value) noexcept
    {
        if (value > target.load (std::memory_order_relaxed))
            target.store (value, std::memory_order_relaxed);
    }

    void storeMin (std::atomic<float>& target, float value) noexcept
    {
        if (value < target.load (std::memory_order_relaxed))
            target.store (value, std::memory_order_relaxed);
    }
}

HeftAudioProcessor::HeftAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout()),
      oversampling (2, tuning::oversamplingFactorLog2),
      dryOversampling (2, tuning::oversamplingFactorLog2),
      clipperOversampling (2, tuning::clipperOversamplingFactorLog2)
{
    fatnessParam    = apvts.getRawParameterValue (ParamIDs::fatness);
    colorParam      = apvts.getRawParameterValue (ParamIDs::color);
    outputParam     = apvts.getRawParameterValue (ParamIDs::output);
    clipperParam    = apvts.getRawParameterValue (ParamIDs::clipper);
    levelMatchParam = apvts.getRawParameterValue (ParamIDs::levelMatch);
    mixParam        = apvts.getRawParameterValue (ParamIDs::mix);

    // The filter latency does not depend on the sample rate, so it can be reported
    // now, before prepareToPlay (some hosts read it when the plugin is opened).
    oversampling.initProcessing (512);
    clipperOversampling.initProcessing (512);
    setLatencySamples (computeLatency());
}

// Total latency = saturation oversampling + clipper oversampling (both always run).
int HeftAudioProcessor::computeLatency()
{
    return juce::roundToInt (oversampling.getLatencyInSamples())
         + juce::roundToInt (clipperOversampling.getLatencyInSamples());
}

//==============================================================================
// Everything that allocates memory or computes constants happens here, never in processBlock.
void HeftAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChunkSize = (size_t) juce::jmax (1, samplesPerBlock);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxChunkSize, 2 };

    compressor.prepare (sampleRate, tuning::compAttackMs, tuning::compReleaseMs);
    preEmphasis.prepare (sampleRate, tuning::tiltPivotHz);
    deEmphasis.prepare (sampleRate, tuning::tiltPivotHz);
    dcBlocker.prepare (sampleRate, tuning::dcBlockerHz);
    inputWeighting.prepare (sampleRate);
    wetWeighting.prepare (sampleRate);
    levelMatcher.prepare (sampleRate, tuning::levelMatchWindowSec, tuning::levelMatchGlideSec,
                          tuning::levelMatchRangeDb, tuning::levelMatchGateDb);

    for (auto* os : { &oversampling, &dryOversampling, &clipperOversampling })
    {
        os->initProcessing (maxChunkSize);
        os->reset();
    }

    const auto totalLatency = computeLatency();
    setLatencySamples (totalLatency);

    // Linear mix: dry and wet are strongly correlated (same source, time-aligned),
    // so 1 - x / x keeps the level steady across the knob. No setWetLatency(): the dry
    // signal is delayed by running it through dryOversampling instead (see pushDry).
    dryWetMixer.prepare (spec);
    dryWetMixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    dryWetMixer.setWetMixProportion (juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f));
    dryHoldLength = juce::roundToInt (0.1 * sampleRate) + (int) maxChunkSize;   // > the mixer's 50 ms fade
    dryHoldSamples = dryHoldLength;
    dryOversamplingActive = true;

    bypassDelay.setMaximumDelayInSamples (totalLatency);
    bypassDelay.prepare (spec);
    bypassDelay.setDelay ((float) totalLatency);

    inputSquared.assign (maxChunkSize, 0.0f);
    clipperWeights.assign (maxChunkSize, 0.0f);
    deCoeffsPerSample.assign (maxChunkSize, {});
    shapePerSample.assign (maxChunkSize, {});
    dryScratch.setSize (2, (int) maxChunkSize);
    bypassDry.setSize (2, (int) maxChunkSize);

    for (auto* smoother : { &fatnessSmoothed, &colorSmoothed, &clipperMix, &levelMatchMix })
        smoother->reset (sampleRate, tuning::paramSmoothingSec);

    outputGain.reset (sampleRate, tuning::paramSmoothingSec);
    bypassMix.reset (sampleRate, tuning::bypassFadeSec);

    updateParameterTargets();

    for (auto* smoother : { &fatnessSmoothed, &colorSmoothed, &clipperMix, &levelMatchMix })
        smoother->setCurrentAndTargetValue (smoother->getTargetValue());

    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    bypassMix.setCurrentAndTargetValue (0.0f);

    lastFatness = lastColor = -1.0f;   // force the coefficients to be computed
    updateCoefficients (fatnessSmoothed.getCurrentValue(), colorSmoothed.getCurrentValue());
}

// After the input stops, the output goes on for the latency plus the DC blocker's decay.
// Hosts that pause plugins in silence use this; with 0 they could cut the end of a sound.
double HeftAudioProcessor::getTailLengthSeconds() const
{
    const auto sampleRate = getSampleRate();
    const auto latencySeconds = sampleRate > 0.0 ? getLatencySamples() / sampleRate : 0.0;
    return latencySeconds + 10.0 / (juce::MathConstants<double>::twoPi * tuning::dcBlockerHz);   // ten time constants
}

bool HeftAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void HeftAudioProcessor::updateParameterTargets() noexcept
{
    fatnessSmoothed.setTargetValue (fatnessParam->load() * 0.01f);
    colorSmoothed.setTargetValue (colorParam->load() * 0.01f);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    clipperMix.setTargetValue (clipperParam->load() >= 0.5f ? 1.0f : 0.0f);
    levelMatchMix.setTargetValue (levelMatchParam->load() >= 0.5f ? 1.0f : 0.0f);
    dryWetMixer.setWetMixProportion (juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f));
}

// Maps Fatness and Color (0..1) to the internal settings of each stage.
void HeftAudioProcessor::updateCoefficients (float fatness, float color) noexcept
{
    if (fatness == lastFatness && color == lastColor)
        return;

    if (fatness != lastFatness)
        compressor.setParameters (fatness * tuning::compThresholdAtMaxDb,
                                  1.0f + fatness * (tuning::compRatioAtMax - 1.0f),
                                  tuning::compKneeDb,
                                  tuning::compMakeupAmount);

    if (color != lastColor)
    {
        const auto highGain = juce::Decibels::decibelsToGain ((2.0f * color - 1.0f) * tuning::tiltMaxDb);
        preCoeffs = preEmphasis.makeCoefficients (highGain);
        deCoeffs  = deEmphasis.makeCoefficients (1.0f / highGain);
    }

    saturatorShape = saturatorShapeFor (fatness, color);

    lastFatness = fatness;
    lastColor   = color;
}

Saturator::Shape HeftAudioProcessor::saturatorShapeFor (float fatness, float color) noexcept
{
    return Saturator::makeShape (fatness * tuning::driveAtMax,
                                 juce::jmap (color, tuning::biasAtMinColor, tuning::biasAtMaxColor),
                                 juce::Decibels::decibelsToGain (fatness * tuning::fatnessMakeupAtMaxDb));
}

//==============================================================================
void HeftAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

// Called by the host's bypass. The chain keeps running so that switching is a
// short crossfade instead of a click, and the DSP is warm when un-bypassed.
void HeftAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

void HeftAudioProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputs = getTotalNumInputChannels();

    for (auto ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateParameterTargets();
    bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);

    const auto numChannels = (size_t) juce::jmin (numInputs, buffer.getNumChannels(), 2);

    if (numChannels == 0 || maxChunkSize == 0)
        return;

    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, numChannels);

    // A plugin before Heft can send NaN, infinity or absurd levels. One such sample would stay
    // in the filters' state (or overflow the loudness measurement) and spoil everything after
    // it, so it becomes silence, and anything above +60 dBFS is capped there.
    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        auto* x = block.getChannelPointer (ch);

        for (size_t i = 0; i < block.getNumSamples(); ++i)
            x[i] = std::isfinite (x[i]) ? juce::jlimit (-1000.0f, 1000.0f, x[i]) : 0.0f;
    }

    // If the host sends a bigger block than announced, process it in chunks so
    // we never exceed what was allocated in prepareToPlay.
    for (size_t start = 0; start < block.getNumSamples(); start += maxChunkSize)
        processChunk (block.getSubBlock (start, juce::jmin (maxChunkSize, block.getNumSamples() - start)));
}

void HeftAudioProcessor::processChunk (juce::dsp::AudioBlock<float> block) noexcept
{
    const auto numSamples  = block.getNumSamples();
    const auto numChannels = block.getNumChannels();
    const auto channelNorm = 1.0f / (float) numChannels;

    // Meter values for this chunk.
    std::array<float, 2> inputPeak {}, outputPeak {};
    float maxReductionDb = 0.0f, matchGain = 1.0f;

    pushDry (block);

    // --- 1. Compressor + 2. pre-emphasis (base sample rate) -----------------
    for (size_t i = 0; i < numSamples; ++i)
    {
        updateCoefficients (fatnessSmoothed.getNextValue(), colorSmoothed.getNextValue());

        float peak = 0.0f, loudness = 0.0f;

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            const auto x = block.getChannelPointer (ch)[i];
            inputPeak[ch] = juce::jmax (inputPeak[ch], std::abs (x));
            peak          = juce::jmax (peak, std::abs (x));

            const auto weighted = inputWeighting.processSample (x, ch);
            loudness += weighted * weighted;

            // Input delayed by the total latency, for the bypass crossfade.
            bypassDelay.pushSample ((int) ch, x);
            bypassDry.setSample ((int) ch, (int) i, bypassDelay.popSample ((int) ch));
        }

        inputSquared[i] = loudness * channelNorm;

        const auto compGain = compressor.processPeak (peak);
        maxReductionDb = juce::jmin (maxReductionDb, compressor.getGainReductionDb());

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& x = block.getChannelPointer (ch)[i];
            x = preEmphasis.processSample (x * compGain, ch, preCoeffs);
        }

        deCoeffsPerSample[i] = deCoeffs;
        shapePerSample[i]    = saturatorShape;
    }

    // --- 3. Saturation inside 4. 4x oversampling -----------------------------
    auto osBlock = oversampling.processSamplesUp (block);

    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        auto* data = osBlock.getChannelPointer (ch);

        for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
            data[i] = Saturator::process (data[i], shapePerSample[i >> tuning::oversamplingFactorLog2]);
    }

    oversampling.processSamplesDown (block);

    // --- 5. De-emphasis + 6. DC blocker + 7. level match (on the wet signal) ---
    for (size_t i = 0; i < numSamples; ++i)
    {
        float loudness = 0.0f;

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& x = block.getChannelPointer (ch)[i];
            x = deEmphasis.processSample (x, ch, deCoeffsPerSample[i]);
            x = dcBlocker.processSample (x, ch);

            const auto weighted = wetWeighting.processSample (x, ch);
            loudness += weighted * weighted;
        }

        matchGain = levelMatcher.process (inputSquared[i], loudness * channelNorm);
        const auto wetGain = 1.0f + levelMatchMix.getNextValue() * (matchGain - 1.0f);

        for (size_t ch = 0; ch < numChannels; ++ch)
            block.getChannelPointer (ch)[i] *= wetGain;
    }

    // --- 8. Mix with the time-aligned dry signal -----------------------------
    dryWetMixer.mixWetSamples (block);

    // --- Output gain, then 9. clipper ------------------------------------------
    for (size_t i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();
        clipperWeights[i] = clipperMix.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
            block.getChannelPointer (ch)[i] *= gain;
    }

    processClipper (block);

    // --- Bypass crossfade and output meter --------------------------------------
    for (size_t i = 0; i < numSamples; ++i)
    {
        const auto bypass = bypassMix.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& x = block.getChannelPointer (ch)[i];
            x += bypass * (bypassDry.getSample ((int) ch, (int) i) - x);
            outputPeak[ch] = juce::jmax (outputPeak[ch], std::abs (x));
        }
    }

    // --- Meters for the editor -------------------------------------------------
    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        storeMax (meters.inputPeak[ch], inputPeak[ch]);
        storeMax (meters.outputPeak[ch], outputPeak[ch]);
    }

    if (bypassMix.getCurrentValue() < 1.0f)
    {
        storeMin (meters.gainReductionDb, maxReductionDb);
        meters.levelMatchDb.store (juce::Decibels::gainToDecibels (matchGain), std::memory_order_relaxed);
    }
}

// Mix: the dry copy goes through the same oversampling filters as the wet signal
// (without the saturation). A plain delay (DryWetMixer::setWetLatency) is only exact
// at low frequencies: the oversampler reaches an integer latency with an
// allpass fractional delay, whose phase differs from a pure delay in the top octaves
// (measured: Mix 50 % lost 1.5 dB at 20 kHz). With identical filters on both paths,
// dry and wet line up at every frequency.
void HeftAudioProcessor::pushDry (juce::dsp::AudioBlock<float> block) noexcept
{
    const auto numSamples = block.getNumSamples();
    auto dry = juce::dsp::AudioBlock<float> (dryScratch).getSubsetChannelBlock (0, block.getNumChannels())
                                                         .getSubBlock (0, numSamples);
    dry.copyFrom (block);

    // At Mix 100 % the dry signal is silent: skip its filters (after the mixer's fade).
    if (mixParam->load() < 100.0f)
        dryHoldSamples = dryHoldLength;
    else
        dryHoldSamples = juce::jmax (0, dryHoldSamples - (int) numSamples);

    if (dryHoldSamples > 0)
    {
        // Coming back: start from silence. The mixer fades the dry signal in, which hides the warm-up.
        if (! dryOversamplingActive)
        {
            dryOversampling.reset();
            dryOversamplingActive = true;
        }

        dryOversampling.processSamplesUp (dry);
        dryOversampling.processSamplesDown (dry);
    }
    else
    {
        dryOversamplingActive = false;
    }

    dryWetMixer.pushDrySamples (dry);
}

// Soft clipper at 4x: less aliasing, and it also catches peaks that fall between
// the base-rate samples. The decimation filter can overshoot slightly, so a hard
// safety clip at the ceiling follows to keep the sample-peak guarantee.
// The 4x filters run even with the clipper off, so switching it never changes the
// latency or the phase response; only the clipping itself fades in and out.
void HeftAudioProcessor::processClipper (juce::dsp::AudioBlock<float> block) noexcept
{
    const auto numSamples  = block.getNumSamples();
    const auto numChannels = block.getNumChannels();
    const auto active      = clipperWeights[0] > 0.0f || clipperWeights[numSamples - 1] > 0.0f;

    auto osBlock = clipperOversampling.processSamplesUp (block);

    if (active)
    {
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = osBlock.getChannelPointer (ch);

            for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
            {
                const auto weight = clipperWeights[i >> tuning::clipperOversamplingFactorLog2];
                data[i] += weight * (SoftClipper::process (data[i], tuning::clipperKnee, tuning::clipperCeiling) - data[i]);
            }
        }
    }

    clipperOversampling.processSamplesDown (block);

    if (active)
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            const auto weight = clipperWeights[i];

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto& x = block.getChannelPointer (ch)[i];
                x += weight * (juce::jlimit (-tuning::clipperCeiling, tuning::clipperCeiling, x) - x);
            }
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* HeftAudioProcessor::createEditor()
{
    return new HeftAudioProcessorEditor (*this);
}

void HeftAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (editorWidthId, editorWidth.load(), nullptr);
    presetManager.writeState (state);   // stateVersion, current preset and both A/B slots

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HeftAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth = (int) state.getProperty (editorWidthId, 0);

            // Presets and A/B travel in their own child. The parameters are restored first,
            // exactly as in 0.3.x; a state without that child (from 0.3.x) is migrated.
            // Memory only: some hosts call this from a thread other than the message thread.
            const auto abState = state.getChildWithName (PresetManager::abStateType);
            state.removeChild (abState, nullptr);
            apvts.replaceState (state);
            presetManager.readState (abState);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HeftAudioProcessor();
}
