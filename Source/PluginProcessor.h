#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/Compressor.h"
#include "DSP/DcBlocker.h"
#include "DSP/KWeighting.h"
#include "DSP/LevelMatcher.h"
#include "DSP/Saturator.h"
#include "DSP/SoftClipper.h"
#include "DSP/TiltFilter.h"
#include "Presets/PresetManager.h"

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

    // Presets and A/B. Declared after apvts, which it uses. Never touched by the audio thread.
    PresetManager presetManager { apvts };

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
    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;

    void process (juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;
    void updateParameterTargets() noexcept;
    void updateCoefficients (float fatness, float color) noexcept;
    void processChunk (juce::dsp::AudioBlock<float> block) noexcept;
    void pushDry (juce::dsp::AudioBlock<float> block) noexcept;
    void processClipper (juce::dsp::AudioBlock<float> block) noexcept;
    int computeLatency();

    // Parameter values (atomic: read lock-free from the audio thread).
    std::atomic<float>* fatnessParam    = nullptr;
    std::atomic<float>* colorParam      = nullptr;
    std::atomic<float>* outputParam     = nullptr;
    std::atomic<float>* clipperParam    = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;
    std::atomic<float>* mixParam        = nullptr;

    // Parameter smoothing to avoid clicks. (Mix is smoothed inside the DryWetMixer.)
    juce::SmoothedValue<float> fatnessSmoothed, colorSmoothed, clipperMix, levelMatchMix, bypassMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    // Processing chain (in order).
    Compressor compressor;                         // 1
    TiltFilter preEmphasis;                        // 2
    juce::dsp::Oversampling<float> oversampling;   // 3 + 4
    TiltFilter deEmphasis;                         // 5
    DcBlocker dcBlocker;                           // 6
    KWeighting inputWeighting, wetWeighting;       // 7: level match measures K-weighted loudness
    LevelMatcher levelMatcher;
    juce::dsp::Oversampling<float> dryOversampling;   // 8: Mix. The dry signal runs through the same
    juce::dsp::DryWetMixer<float> dryWetMixer;        //    filters as the wet one, so both line up exactly
    juce::dsp::Oversampling<float> clipperOversampling;   // 9: clipper at 4x

    // Host bypass: the full chain keeps running and we crossfade to the delayed input.
    Delay bypassDelay;

    // State derived from Fatness/Color (recomputed only when they change).
    float lastFatness = -1.0f, lastColor = -1.0f;
    TiltFilter::Coefficients preCoeffs, deCoeffs;
    Saturator::Shape saturatorShape;

    // The dry filters only run while the dry signal is audible (Mix below 100 %, plus
    // the mixer's fade time), to save CPU at the default Mix of 100 %.
    int dryHoldSamples = 0, dryHoldLength = 0;
    bool dryOversamplingActive = false;

    // Per-sample values handed from one stage to the next (allocated in prepareToPlay).
    std::vector<float> inputSquared, clipperWeights;
    std::vector<TiltFilter::Coefficients> deCoeffsPerSample;
    std::vector<Saturator::Shape> shapePerSample;
    juce::AudioBuffer<float> dryScratch, bypassDry;
    size_t maxChunkSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeftAudioProcessor)
};
