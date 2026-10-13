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
      oversampling (2, tuning::oversamplingFactorLog2,
                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                    true,   // maximum filter quality
                    true)   // integer latency (hosts only accept whole samples)
{
    fatnessParam    = apvts.getRawParameterValue (ParamIDs::fatness);
    colorParam      = apvts.getRawParameterValue (ParamIDs::color);
    outputParam     = apvts.getRawParameterValue (ParamIDs::output);
    clipperParam    = apvts.getRawParameterValue (ParamIDs::clipper);
    levelMatchParam = apvts.getRawParameterValue (ParamIDs::levelMatch);

    // The filter latency does not depend on the sample rate, so it can be reported
    // now, before prepareToPlay (some hosts read it when the plugin is opened).
    oversampling.initProcessing (512);
    setLatencySamples (juce::roundToInt (oversampling.getLatencyInSamples()));
}

//==============================================================================
// Everything that allocates memory or computes constants happens here, never in processBlock.
void HeftAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChunkSize = (size_t) juce::jmax (1, samplesPerBlock);

    compressor.prepare (sampleRate, tuning::compAttackMs, tuning::compReleaseMs);
    preEmphasis.prepare (sampleRate, tuning::tiltPivotHz);
    deEmphasis.prepare (sampleRate, tuning::tiltPivotHz);
    dcBlocker.prepare (sampleRate, tuning::dcBlockerHz);
    levelMatcher.prepare (sampleRate, tuning::levelMatchWindowSec, tuning::levelMatchGlideSec,
                          tuning::levelMatchRangeDb, tuning::levelMatchGateDb);

    oversampling.initProcessing (maxChunkSize);
    oversampling.reset();

    const auto latency = juce::roundToInt (oversampling.getLatencyInSamples());
    setLatencySamples (latency);

    bypassDelay.setMaximumDelayInSamples (latency);
    bypassDelay.prepare ({ sampleRate, (juce::uint32) maxChunkSize, 2 });
    bypassDelay.setDelay ((float) latency);

    inputSquared.assign (maxChunkSize, 0.0f);
    deCoeffsPerSample.assign (maxChunkSize, {});
    shapePerSample.assign (maxChunkSize, {});

    fatnessSmoothed.reset (sampleRate, tuning::paramSmoothingSec);
    colorSmoothed.reset (sampleRate, tuning::paramSmoothingSec);
    outputGain.reset (sampleRate, tuning::paramSmoothingSec);
    clipperMix.reset (sampleRate, tuning::paramSmoothingSec);
    levelMatchMix.reset (sampleRate, tuning::paramSmoothingSec);

    updateParameterTargets();
    fatnessSmoothed.setCurrentAndTargetValue (fatnessSmoothed.getTargetValue());
    colorSmoothed.setCurrentAndTargetValue (colorSmoothed.getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    clipperMix.setCurrentAndTargetValue (clipperMix.getTargetValue());
    levelMatchMix.setCurrentAndTargetValue (levelMatchMix.getTargetValue());

    lastFatness = lastColor = -1.0f;   // force the coefficients to be computed
    updateCoefficients (fatnessSmoothed.getCurrentValue(), colorSmoothed.getCurrentValue());
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
    juce::ScopedNoDenormals noDenormals;

    const auto numInputs = getTotalNumInputChannels();

    for (auto ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateParameterTargets();

    const auto numChannels = (size_t) juce::jmin (numInputs, buffer.getNumChannels(), 2);

    if (numChannels == 0 || maxChunkSize == 0)
        return;

    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, numChannels);

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

    // --- 1. Compressor + 2. pre-emphasis (base sample rate) -----------------
    for (size_t i = 0; i < numSamples; ++i)
    {
        updateCoefficients (fatnessSmoothed.getNextValue(), colorSmoothed.getNextValue());

        float peak = 0.0f, power = 0.0f;

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            const auto x = block.getChannelPointer (ch)[i];
            inputPeak[ch] = juce::jmax (inputPeak[ch], std::abs (x));
            peak          = juce::jmax (peak, std::abs (x));
            power        += x * x;

            // Keep the bypass line fed so that switching to bypass doesn't cut out.
            bypassDelay.pushSample ((int) ch, x);
            bypassDelay.popSample ((int) ch);
        }

        inputSquared[i] = power * channelNorm;

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

    // --- 5. De-emphasis + 6. DC blocker + 7. output ---------------------------
    for (size_t i = 0; i < numSamples; ++i)
    {
        float power = 0.0f;

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& x = block.getChannelPointer (ch)[i];
            x = deEmphasis.processSample (x, ch, deCoeffsPerSample[i]);
            x = dcBlocker.processSample (x, ch);
            power += x * x;
        }

        matchGain       = levelMatcher.process (inputSquared[i], power * channelNorm);
        const auto gain = outputGain.getNextValue() * (1.0f + levelMatchMix.getNextValue() * (matchGain - 1.0f));
        const auto clip = clipperMix.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& x = block.getChannelPointer (ch)[i];
            x *= gain;

            if (clip > 0.0f)
                x += clip * (SoftClipper::process (x, tuning::clipperKnee, tuning::clipperCeiling) - x);

            outputPeak[ch] = juce::jmax (outputPeak[ch], std::abs (x));
        }
    }

    // --- Meters for the editor -------------------------------------------------
    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        storeMax (meters.inputPeak[ch], inputPeak[ch]);
        storeMax (meters.outputPeak[ch], outputPeak[ch]);
    }

    storeMin (meters.gainReductionDb, maxReductionDb);
    meters.levelMatchDb.store (juce::Decibels::gainToDecibels (matchGain), std::memory_order_relaxed);
}

void HeftAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputs = getTotalNumInputChannels();

    for (auto ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const auto numChannels = (size_t) juce::jmin (numInputs, buffer.getNumChannels(), 2);

    if (numChannels == 0)
        return;

    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, numChannels);
    bypassDelay.process (juce::dsp::ProcessContextReplacing<float> (block));
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
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HeftAudioProcessor();
}
