#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/Compressor.h"
#include "DSP/DcBlocker.h"
#include "DSP/LevelMatcher.h"
#include "DSP/Saturator.h"
#include "DSP/SoftClipper.h"
#include "DSP/TiltFilter.h"

class HeftAudioProcessor final : public juce::AudioProcessor
{
public:
    HeftAudioProcessor();
    ~HeftAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Saturator settings for a given Fatness and Color (both 0..1). Shared with the
    // editor's curve display so it always draws exactly what the audio path uses.
    static Saturator::Shape saturatorShapeFor (float fatness, float color) noexcept;

    // Meter values for the editor. The audio thread accumulates the peaks since
    // the last read; the editor reads and resets them with exchange(). Lock-free.
    struct Meters
    {
        std::array<std::atomic<float>, 2> inputPeak {}, outputPeak {};   // linear
        std::atomic<float> gainReductionDb { 0.0f };                     // <= 0
        std::atomic<float> levelMatchDb { 0.0f };                        // latest correction
    };

    Meters meters;

    // Editor width in pixels (0 = default), saved with the project.
    std::atomic<int> editorWidth { 0 };

private:
    void updateParameterTargets() noexcept;
    void updateCoefficients (float fatness, float color) noexcept;
    void processChunk (juce::dsp::AudioBlock<float> block) noexcept;

    // Parameter values (atomic: read lock-free from the audio thread).
    std::atomic<float>* fatnessParam    = nullptr;
    std::atomic<float>* colorParam      = nullptr;
    std::atomic<float>* outputParam     = nullptr;
    std::atomic<float>* clipperParam    = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;

    // Parameter smoothing to avoid clicks.
    juce::SmoothedValue<float> fatnessSmoothed, colorSmoothed, clipperMix, levelMatchMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    // Processing chain (in order).
    Compressor compressor;                     // 1
    TiltFilter preEmphasis;                    // 2
    juce::dsp::Oversampling<float> oversampling; // 3 + 4
    TiltFilter deEmphasis;                     // 5
    DcBlocker dcBlocker;                       // 6
    LevelMatcher levelMatcher;                 // 7

    // Bypass with the same latency as the processed path, so it stays time-aligned.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypassDelay;

    // State derived from Fatness/Color (recomputed only when they change).
    float lastFatness = -1.0f, lastColor = -1.0f;
    TiltFilter::Coefficients preCoeffs, deCoeffs;
    Saturator::Shape saturatorShape;

    // Per-sample values handed from one stage to the next (allocated in prepareToPlay).
    std::vector<float> inputSquared;
    std::vector<TiltFilter::Coefficients> deCoeffsPerSample;
    std::vector<Saturator::Shape> shapePerSample;
    size_t maxChunkSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeftAudioProcessor)
};
