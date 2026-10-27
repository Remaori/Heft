// HeftMeasure: loads Heft.vst3 the way a DAW would, feeds it test signals and
// measures the result. Use it to check that changes to Tuning.h don't break
// anything: latency, transparency, dry/wet alignment, clipper peaks (sample and
// true peak), level match (K-weighted loudness), bypass crossfade and CPU use.
//
// Usage:  HeftMeasure.exe "path\to\Heft.vst3"
//
// Null test (proves a change doesn't touch the sound): render fixed signals on main
// first, then compare after the change. A pure refactor must be bit-identical, or
// differ by less than -140 dBFS (rounding from reordered operations).
//         HeftMeasure.exe "path\to\Heft.vst3" --null-render  build\null\main
//         HeftMeasure.exe "path\to\Heft.vst3" --null-compare build\null\main

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/KWeighting.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr double kPi         = juce::MathConstants<double>::pi;

struct Settings
{
    float fatness = 0.0f, color = 50.0f, outputDb = 0.0f, mix = 100.0f;
    bool  clipper = false, levelMatch = false;
};

class Host
{
public:
    explicit Host (std::unique_ptr<juce::AudioPluginInstance> p, double sampleRate = kSampleRate, int maxBlockSize = kBlockSize)
        : plugin (std::move (p)), blockSize (maxBlockSize)
    {
        for (auto* param : plugin->getParameters())
            params[param->getName (64)] = param;

        plugin->prepareToPlay (sampleRate, blockSize);
    }

    ~Host() { plugin->releaseResources(); }

    bool apply (const Settings& s)
    {
        return set ("Fatness", s.fatness / 100.0f)
            && set ("Color", s.color / 100.0f)
            && set ("Output", (s.outputDb + 24.0f) / 36.0f)
            && set ("Mix", s.mix / 100.0f)
            && set ("Clipper", s.clipper ? 1.0f : 0.0f)
            && set ("Level Match", s.levelMatch ? 1.0f : 0.0f);
    }

    // Same signal on both channels. Returns the processed left channel.
    // beforeBlock (if given) is called with the start sample of every block.
    std::vector<float> run (const std::vector<float>& input,
                            const std::function<void (size_t)>& beforeBlock = {})
    {
        std::vector<float> output (input.size());
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (size_t start = 0; start < input.size(); start += (size_t) blockSize)
        {
            if (beforeBlock)
                beforeBlock (start);

            const auto n = (int) std::min<size_t> ((size_t) blockSize, input.size() - start);
            buffer.setSize (2, n, false, false, true);

            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, input.data() + start, n);

            plugin->processBlock (buffer, midi);

            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, output.begin() + (long) start);
        }

        return output;
    }

    // The host-side bypass (the VST3 "bypass" parameter). False if the plugin has none.
    bool setBypass (bool shouldBypass)
    {
        auto* bypass = plugin->getBypassParameter();

        if (bypass == nullptr)
            return false;

        bypass->setValueNotifyingHost (shouldBypass ? 1.0f : 0.0f);
        return true;
    }

    int latency() const { return plugin->getLatencySamples(); }

private:
    bool set (const juce::String& name, float normalised)
    {
        auto it = params.find (name);

        if (it == params.end())
        {
            std::printf ("ERROR: parameter not found: \"%s\"\n", name.toRawUTF8());
            return false;
        }

        it->second->setValueNotifyingHost (normalised);
        return true;
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    int blockSize;
    std::map<juce::String, juce::AudioProcessorParameter*> params;
};

std::vector<float> sine (double freq, double amplitude, double seconds, double phase = 0.0)
{
    std::vector<float> s ((size_t) (seconds * kSampleRate));

    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (float) (amplitude * std::sin (2.0 * kPi * freq * (double) i / kSampleRate + phase));

    return s;
}

// Kick-like hits (decaying sine at "freq") every 250 ms, peaking at "peak".
std::vector<float> drumHits (double peak, double seconds, double freq = 60.0)
{
    std::vector<float> s ((size_t) (seconds * kSampleRate));
    const auto period = (size_t) (0.25 * kSampleRate);

    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto t = (double) (i % period) / kSampleRate;
        s[i] = (float) (peak * std::exp (-t / 0.06) * std::sin (2.0 * kPi * freq * t));
    }

    return s;
}

std::vector<float> noise (double amplitude, double seconds)
{
    juce::Random random (1234);
    std::vector<float> s ((size_t) (seconds * kSampleRate));

    for (auto& x : s)
        x = (float) (amplitude * (2.0 * random.nextDouble() - 1.0));

    return s;
}

struct Analysis
{
    double fundamental = 0, phase = 0, thdnDb = 0, h2Db = 0, h3Db = 0, peak = 0, dc = 0;
    bool finite = true;
};

// Analyses the last second (a whole number of cycles for integer frequencies in Hz).
Analysis analyse (const std::vector<float>& y, double f0)
{
    const auto n     = (size_t) kSampleRate;
    const auto* data = y.data() + (y.size() - n);

    Analysis a;

    for (size_t i = 0; i < n; ++i)
    {
        a.finite = a.finite && std::isfinite (data[i]);
        a.dc    += data[i];
        a.peak   = std::max (a.peak, (double) std::abs (data[i]));
    }

    a.dc /= (double) n;

    auto project = [&] (double f, double& s, double& c)
    {
        s = c = 0.0;

        for (size_t i = 0; i < n; ++i)
        {
            const auto w = 2.0 * kPi * f * (double) i / kSampleRate;
            s += data[i] * std::sin (w);
            c += data[i] * std::cos (w);
        }

        s *= 2.0 / (double) n;
        c *= 2.0 / (double) n;
        return std::sqrt (s * s + c * c);
    };

    double s1, c1, s2, c2, s3, c3;
    a.fundamental = project (f0, s1, c1);
    a.phase       = std::atan2 (c1, s1);
    const auto h2 = project (2.0 * f0, s2, c2);
    const auto h3 = project (3.0 * f0, s3, c3);

    double residual = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto w = 2.0 * kPi * f0 * (double) i / kSampleRate;
        const auto r = data[i] - a.dc - (s1 * std::sin (w) + c1 * std::cos (w));
        residual += r * r;
    }

    const auto toDb = [] (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); };
    a.thdnDb = toDb (std::sqrt (residual / (double) n) / (a.fundamental / std::sqrt (2.0)));
    a.h2Db   = toDb (h2 / a.fundamental);
    a.h3Db   = toDb (h3 / a.fundamental);
    return a;
}

double rmsDb (const std::vector<float>& y)
{
    const auto n = (size_t) kSampleRate;
    double sum = 0.0;

    for (size_t i = y.size() - n; i < y.size(); ++i)
        sum += (double) y[i] * y[i];

    return 10.0 * std::log10 (std::max (sum / (double) n, 1.0e-24));
}

// K-weighted mean square of the last second, in dB (LUFS up to a constant offset).
double loudnessDb (const std::vector<float>& y)
{
    KWeighting weighting;
    weighting.prepare (kSampleRate);
    const auto n = (size_t) kSampleRate;
    double sum = 0.0;

    for (size_t i = 0; i < y.size(); ++i)
    {
        const auto k = (double) weighting.processSample (y[i], 0);

        if (i >= y.size() - n)
            sum += k * k;
    }

    return 10.0 * std::log10 (std::max (sum / (double) n, 1.0e-24));
}

// Highest absolute value of the signal reconstructed at 4x, over the last second.
double truePeak (const std::vector<float>& y)
{
    juce::dsp::Oversampling<float> os (1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os.initProcessing (kBlockSize);

    const auto start = y.size() - (size_t) kSampleRate;
    std::vector<float> chunk ((size_t) kBlockSize);
    double peak = 0.0;

    for (size_t pos = 0; pos < y.size(); pos += kBlockSize)
    {
        const auto n = std::min<size_t> (kBlockSize, y.size() - pos);
        std::copy (y.begin() + (long) pos, y.begin() + (long) (pos + n), chunk.begin());

        float* channels[] = { chunk.data() };
        juce::dsp::AudioBlock<float> block (channels, 1, n);
        const auto up = os.processSamplesUp (block);

        if (pos >= start)
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                peak = std::max (peak, (double) std::abs (up.getSample (0, (int) i)));

        os.processSamplesDown (block);
    }

    return peak;
}

double toDbfs (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    failures += ok ? 0 : 1;
}

//==============================================================================
// Null test: fixed renders, each from a fresh instance so nothing carries over.
using MakeHost = std::function<std::unique_ptr<Host>()>;

std::vector<float> sweep (double amplitude, double seconds)
{
    std::vector<float> s ((size_t) (seconds * kSampleRate));
    const auto k = std::log (20000.0 / 20.0);

    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto t = (double) i / kSampleRate;
        s[i] = (float) (amplitude * std::sin (2.0 * kPi * 20.0 * seconds / k * (std::exp (t / seconds * k) - 1.0)));
    }

    return s;
}

std::vector<std::pair<juce::String, std::vector<float>>> nullRenders (const MakeHost& makeHost)
{
    const std::pair<const char*, Settings> settings[] {
        { "default", { 25.0f, 50.0f, 0.0f, 100.0f, false, false } },
        { "allon",   { 60.0f, 60.0f, 0.0f, 70.0f, true, true } },
        { "extreme", { 100.0f, 100.0f, 6.0f, 100.0f, true, false } },
        { "dark",    { 80.0f, 0.0f, -3.0f, 50.0f, false, true } } };

    auto burst = noise (0.1, 4.0);

    for (size_t i = (size_t) kSampleRate; i < (size_t) (1.5 * kSampleRate); ++i)
        burst[i] *= 40.0f;   // half a second with peaks at +12 dBFS

    const std::pair<const char*, std::vector<float>> signals[] {
        { "sweep", sweep (0.5, 6.0) },
        { "noise", noise (0.25, 6.0) },
        { "kick", drumHits (0.9, 6.0) },
        { "silence", std::vector<float> ((size_t) (3.0 * kSampleRate), 0.0f) },
        { "burst", burst } };

    std::vector<std::pair<juce::String, std::vector<float>>> renders;

    for (const auto& [signalName, input] : signals)
        for (const auto& [settingsName, s] : settings)
        {
            auto host = makeHost();
            host->apply (s);
            renders.push_back ({ juce::String (signalName) + "_" + settingsName, host->run (input) });
        }

    // Automation: Fatness and Color move every block for 2 s from the middle, then stay.
    auto automation = drumHits (0.9, 8.0);
    const auto bed = noise (0.05, 8.0);

    for (size_t i = 0; i < automation.size(); ++i)
        automation[i] += bed[i];

    for (const auto& [settingsName, s] : { settings[0], settings[1] })
    {
        auto host = makeHost();
        host->apply (s);
        const auto half = automation.size() / 2;

        renders.push_back ({ juce::String ("automation_") + settingsName, host->run (automation, [&] (size_t start)
        {
            if (start < half)
                return;

            const auto t = (float) std::min (1.0, (double) (start - half) / (2.0 * kSampleRate));
            auto moved = s;
            moved.fatness += t * (95.0f - s.fatness);
            moved.color   += t * (5.0f - s.color);
            host->apply (moved);
        }) });
    }

    return renders;
}

int nullRender (const MakeHost& makeHost, const juce::File& folder)
{
    folder.createDirectory();

    for (const auto& [name, output] : nullRenders (makeHost))
        folder.getChildFile (name + ".f32").replaceWithData (output.data(), output.size() * sizeof (float));

    std::printf ("Null-test renders written to %s\n", folder.getFullPathName().toRawUTF8());
    return 0;
}

int nullCompare (const MakeHost& makeHost, const juce::File& folder)
{
    int changed = 0;
    std::printf ("\n== Null test against %s ==\n", folder.getFullPathName().toRawUTF8());

    for (const auto& [name, output] : nullRenders (makeHost))
    {
        juce::MemoryBlock reference;

        if (! folder.getChildFile (name + ".f32").loadFileAsData (reference) || reference.getSize() != output.size() * sizeof (float))
        {
            std::printf ("  %-22s missing or a different length\n", name.toRawUTF8());
            ++changed;
            continue;
        }

        const auto* ref = static_cast<const float*> (reference.getData());
        double maxDiff = 0.0;

        for (size_t i = 0; i < output.size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (output[i] - ref[i]));

        if (std::memcmp (ref, output.data(), reference.getSize()) == 0)
            std::printf ("  %-22s bit-identical\n", name.toRawUTF8());
        else if (maxDiff < 1.0e-7)   // -140 dBFS
            std::printf ("  %-22s rounding only, max diff %.1f dBFS\n", name.toRawUTF8(), toDbfs (maxDiff));
        else
        {
            std::printf ("  %-22s CHANGED, max diff %.1f dBFS\n", name.toRawUTF8(), toDbfs (maxDiff));
            ++changed;
        }
    }

    std::printf ("\n%s\n", changed == 0 ? "NULL TEST PASSED" : "NULL TEST: THE OUTPUT CHANGED");
    return changed == 0 ? 0 : 1;
}

int measureLatency (Host& host, const Settings& settings)
{
    host.apply (settings);
    host.run (std::vector<float> ((size_t) kSampleRate, 0.0f));   // let the smoothing settle

    std::vector<float> impulse ((size_t) kSampleRate / 2, 0.0f);
    const size_t at = 1000;
    impulse[at] = 0.5f;
    const auto y = host.run (impulse);

    size_t peakIndex = 0;
    for (size_t i = 0; i < y.size(); ++i)
        if (std::abs (y[i]) > std::abs (y[peakIndex]))
            peakIndex = i;

    return (int) peakIndex - (int) at;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf ("Usage: HeftMeasure \"path\\to\\Heft.vst3\"\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::String::fromUTF8 (argv[1]));

    if (types.isEmpty())
    {
        std::printf ("ERROR: could not read the plugin at %s\n", argv[1]);
        return 2;
    }

    juce::String error;
    const auto makeHostAt = [&] (double sampleRate, int blockSize) -> std::unique_ptr<Host>
    {
        auto instance = format.createInstanceFromDescription (*types[0], sampleRate, blockSize, error);
        return instance != nullptr ? std::make_unique<Host> (std::move (instance), sampleRate, blockSize) : nullptr;
    };

    auto mainHost = makeHostAt (kSampleRate, kBlockSize);

    if (mainHost == nullptr)
    {
        std::printf ("ERROR loading the plugin: %s\n", error.toRawUTF8());
        return 2;
    }

    if (argc >= 4)
    {
        const juce::String mode (argv[2]);
        const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[3]));
        const MakeHost makeHost = [&] { return makeHostAt (kSampleRate, kBlockSize); };

        if (mode == "--null-render")
            return nullRender (makeHost, folder);

        if (mode == "--null-compare")
            return nullCompare (makeHost, folder);
    }

    auto& host = *mainHost;
    const auto amp = 0.5;   // -6 dBFS peak

    // --- Latency --------------------------------------------------------------
    std::printf ("\n== Latency ==\n");
    {
        // The impulse gives a rough figure (the filters' fractional-delay allpass can move
        // the peak by a sample); the phase of a 1 kHz sine gives the exact delay.
        const auto delayAt1k = [&] (const Settings& settings, int roughSamples)
        {
            host.apply (settings);
            const auto in  = sine (1000.0, amp, 2.0);
            const auto out = analyse (host.run (in), 1000.0);
            const auto ref = analyse (in, 1000.0);
            const auto period = kSampleRate / 1000.0;
            const auto phaseDelay = (ref.phase - out.phase) / (2.0 * kPi) * period;
            return roughSamples + std::remainder (phaseDelay - roughSamples, period);
        };

        bool allOk = true;

        for (const auto& [name, settings] : { std::pair { "wet          ", Settings { 0.0f, 50.0f, 0.0f, 100.0f, false, false } },
                                              std::pair { "dry (Mix 0 %)", Settings { 0.0f, 50.0f, 0.0f, 0.0f, false, false } },
                                              std::pair { "clipper on   ", Settings { 0.0f, 50.0f, 0.0f, 100.0f, true, false } } })
        {
            const auto rough = measureLatency (host, settings);
            const auto exact = delayAt1k (settings, rough);
            std::printf ("  %s impulse peak %d, delay at 1 kHz %.2f samples\n", name, rough, exact);
            allOk = allOk && std::abs (exact - host.latency()) < 0.1;
        }

        std::printf ("  Reported to the host: %d samples (%.2f ms at 48 kHz)\n", host.latency(), 1000.0 * host.latency() / kSampleRate);
        check (allOk, "reported latency matches the real delay (wet, dry and clipper paths, within 0.1 samples)");
    }

    // --- Transparency at Fatness 0 -----------------------------------------
    std::printf ("\n== Fatness 0 %% (must be transparent) ==\n");
    {
        bool allOk = true;

        for (auto color : { 0.0f, 50.0f, 100.0f })
        {
            for (auto freq : { 100.0, 1000.0, 10000.0 })
            {
                host.apply ({ 0.0f, color, 0.0f, 100.0f, false, false });
                const auto a     = analyse (host.run (sine (freq, amp, 2.0)), freq);
                const auto delta = 20.0 * std::log10 (a.fundamental / amp);
                std::printf ("  Color %3.0f%%  %6.0f Hz: level change %+7.3f dB, THD+N %7.1f dB\n", color, freq, delta, a.thdnDb);
                allOk = allOk && a.finite && std::abs (delta) < 0.1 && a.thdnDb < -80.0;
            }
        }

        check (allOk, "level within ±0.1 dB and THD+N below -80 dB");

        // Informational: the oversampling filters roll off the extreme highs.
        std::printf ("  Extreme highs (48 kHz):");

        for (auto freq : { 16000.0, 18000.0, 20000.0, 21000.0, 22000.0 })
        {
            host.apply ({ 0.0f, 50.0f, 0.0f, 100.0f, false, false });
            const auto a = analyse (host.run (sine (freq, amp, 2.0)), freq);
            std::printf ("  %.0f kHz %+.2f dB", freq / 1000.0, 20.0 * std::log10 (a.fundamental / amp));
        }

        std::printf ("\n");
    }

    // --- Mix ------------------------------------------------------------------
    std::printf ("\n== Mix (dry/wet) ==\n");
    {
        // Dry and wet must be time-aligned: at Fatness 0 any Mix value stays transparent.
        // A misaligned dry path would comb-filter, which shows up most at 10 kHz.
        bool aligned = true;

        for (auto freq : { 100.0, 1000.0, 10000.0, 15000.0, 20000.0 })
        {
            host.apply ({ 0.0f, 50.0f, 0.0f, 50.0f, false, false });
            const auto a     = analyse (host.run (sine (freq, amp, 2.0)), freq);
            const auto delta = 20.0 * std::log10 (a.fundamental / amp);
            std::printf ("  Fatness 0 %%, Mix 50 %%  %6.0f Hz: level change %+7.3f dB, THD+N %7.1f dB\n", freq, delta, a.thdnDb);
            aligned = aligned && std::abs (delta) < 0.1 && a.thdnDb < -80.0;
        }

        check (aligned, "dry and wet are time-aligned (Mix 50 % at Fatness 0 stays within ±0.1 dB)");

        host.apply ({ 100.0f, 100.0f, 0.0f, 0.0f, false, false });
        const auto dry = analyse (host.run (sine (1000.0, amp, 2.0)), 1000.0);
        std::printf ("  Fatness 100 %%, Mix 0 %%: level change %+.3f dB, THD+N %.1f dB\n",
                     20.0 * std::log10 (dry.fundamental / amp), dry.thdnDb);
        check (std::abs (20.0 * std::log10 (dry.fundamental / amp)) < 0.05 && dry.thdnDb < -80.0,
               "Mix 0 % returns the clean input even at Fatness 100 %");

        std::printf ("  Kick-like hits, Fatness 100 %%: RMS change by Mix:");
        const auto input = drumHits (amp, 3.0);

        for (auto mix : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            host.apply ({ 100.0f, 50.0f, 0.0f, mix, false, false });
            std::printf ("  %.0f%% %+.2f dB", mix, rmsDb (host.run (input)) - rmsDb (input));
        }

        std::printf ("\n");
    }

    // --- Character -----------------------------------------------------------
    std::printf ("\n== Character with a 1 kHz sine at -6 dBFS ==\n");
    std::printf ("  Fatness  Color   Level dB   THD+N dB   H2 dB    H3 dB    Peak    DC\n");
    {
        bool allFinite = true;

        for (auto fatness : { 25.0f, 50.0f, 100.0f })
        {
            for (auto color : { 0.0f, 50.0f, 100.0f })
            {
                host.apply ({ fatness, color, 0.0f, 100.0f, false, false });
                const auto a = analyse (host.run (sine (1000.0, amp, 2.0)), 1000.0);
                std::printf ("  %5.0f%%  %4.0f%%   %+7.2f   %8.1f  %7.1f  %7.1f   %5.3f  %+.4f\n",
                             fatness, color, 20.0 * std::log10 (a.fundamental / amp), a.thdnDb, a.h2Db, a.h3Db, a.peak, a.dc);
                allFinite = allFinite && a.finite && std::abs (a.dc) < 1.0e-3;
            }
        }

        check (allFinite, "no NaN/Inf and no noticeable DC offset");
    }

    // --- Transient material (informational) ------------------------------------------
    std::printf ("\n== Kick-like hits (-6 dBFS peak): RMS and peak level change ==\n");
    {
        const auto input = drumHits (amp, 3.0);
        const auto inRms = rmsDb (input);

        for (auto fatness : { 25.0f, 50.0f, 75.0f, 100.0f })
        {
            host.apply ({ fatness, 50.0f, 0.0f, 100.0f, false, false });
            const auto y = host.run (input);
            const auto a = analyse (y, 60.0);
            std::printf ("  Fatness %3.0f%%: RMS %+6.2f dB, peak %+6.2f dB\n",
                         fatness, rmsDb (y) - inRms, 20.0 * std::log10 (a.peak / amp));
        }
    }

    // --- Clipper -----------------------------------------------------------------------
    std::printf ("\n== Clipper ==\n");
    {
        // Below the knee the clipper must not change anything, including the timing:
        // its 4x path and the delay used while it's off must line up exactly
        // (one sample at 10 kHz would be 75 degrees).
        bool transparent = true;

        for (auto freq : { 1000.0, 10000.0 })
        {
            host.apply ({ 0.0f, 50.0f, 0.0f, 100.0f, false, false });
            const auto off = analyse (host.run (sine (freq, 0.3, 2.0)), freq);
            host.apply ({ 0.0f, 50.0f, 0.0f, 100.0f, true, false });
            const auto on = analyse (host.run (sine (freq, 0.3, 2.0)), freq);

            const auto levelDb  = 20.0 * std::log10 (on.fundamental / off.fundamental);
            const auto phaseDeg = std::remainder (on.phase - off.phase, 2.0 * kPi) * 180.0 / kPi;
            std::printf ("  Below the knee, %5.0f Hz: clipper on vs off %+.3f dB, %+.2f degrees, THD+N %.1f dB\n",
                         freq, levelDb, phaseDeg, on.thdnDb);
            transparent = transparent && std::abs (levelDb) < 0.1 && std::abs (phaseDeg) < 2.0 && on.thdnDb < -80.0;
        }

        check (transparent, "below the knee the clipper changes nothing (same level and phase as with it off)");

        struct Case { const char* name; std::vector<float> input; };

        // A 12 kHz sine with a 45° phase has its samples at 0.71 of the true peak: the
        // classic inter-sample peak test.
        const Case cases[] = { { "200 Hz, input -0.9 dBFS  ", sine (200.0, 0.9, 2.0) },
                               { "12 kHz at 45°, input -0.9 dBTP", sine (12000.0, 0.9, 2.0, 0.25 * kPi) },
                               { "kick hits, input -0.9 dBFS ", drumHits (0.9, 3.0) } };

        bool samplePeaksOk = true, truePeaksOk = true;

        for (const auto& c : cases)
        {
            host.apply ({ 50.0f, 50.0f, 12.0f, 100.0f, true, false });
            const auto y = host.run (c.input);
            const auto a = analyse (y, 200.0);
            const auto tp = truePeak (y);
            std::printf ("  Fatness 50 %%, Output +12 dB, %s: sample peak %.4f (%+.2f dBFS), true peak %+.2f dBTP\n",
                         c.name, a.peak, toDbfs (a.peak), toDbfs (tp));
            samplePeaksOk = samplePeaksOk && a.peak <= 1.0;
            truePeaksOk   = truePeaksOk && toDbfs (tp) <= 0.5;
        }

        check (samplePeaksOk, "sample peaks never exceed 0 dBFS with the clipper on");
        check (truePeaksOk, "true peaks (4x reconstruction) stay below +0.5 dBTP");
    }

    // --- Level Match -----------------------------------------------------------------
    std::printf ("\n== Level Match (compared as K-weighted loudness, like LUFS) ==\n");
    {
        struct Case { float fatness, color; const char* name; std::vector<float> input; };

        const Case cases[] = { { 100.0f, 100.0f, "1 kHz sine, -6 dBFS   ", sine (1000.0, 0.5, 4.0) },
                               { 100.0f,   0.0f, "100 Hz sine, -6 dBFS  ", sine (100.0, 0.5, 4.0) },
                               {  60.0f,  50.0f, "440 Hz sine, -20 dBFS ", sine (440.0, 0.1, 4.0) },
                               {  80.0f,  30.0f, "55 Hz kick hits      ", drumHits (0.5, 4.0, 55.0) } };

        bool allOk = true;

        for (const auto& c : cases)
        {
            host.apply ({ c.fatness, c.color, 0.0f, 100.0f, false, false });
            const auto off = host.run (c.input);

            host.apply ({ c.fatness, c.color, 0.0f, 100.0f, false, true });
            const auto on = host.run (c.input);

            const auto inLoudness = loudnessDb (c.input);
            std::printf ("  Fatness %3.0f%% Color %3.0f%% %s: loudness change unmatched %+6.2f dB, matched %+6.2f dB (RMS %+6.2f dB)\n",
                         c.fatness, c.color, c.name, loudnessDb (off) - inLoudness, loudnessDb (on) - inLoudness,
                         rmsDb (on) - rmsDb (c.input));
            allOk = allOk && std::abs (loudnessDb (on) - inLoudness) < 0.5;
        }

        check (allOk, "with Level Match the loudness difference stays below ±0.5 dB");
    }

    // --- Bypass ------------------------------------------------------------------------
    std::printf ("\n== Bypass (host) ==\n");
    {
        // Wet is 12 dB louder than dry, and we switch where the sine is near its peak:
        // a hard switch would jump by ~0.5; the crossfade keeps every step small.
        host.apply ({ 0.0f, 50.0f, 12.0f, 100.0f, false, false });
        const auto input = sine (50.0, 0.2, 3.0);
        bool switched = false, supported = true;

        const auto y = host.run (input, [&] (size_t start)
        {
            const auto nearPeak = std::abs (input[start]) > 0.18f;

            if (! switched && start >= 48000 && nearPeak)
            {
                supported = host.setBypass (true);
                switched = true;
            }
            else if (switched && start >= 96000 && nearPeak)
            {
                host.setBypass (false);
            }
        });

        host.setBypass (false);

        if (! supported)
        {
            std::printf ("  (the hosted plugin exposes no bypass parameter: skipped)\n");
        }
        else
        {
            double maxStep = 0.0;

            for (size_t i = 24000; i < y.size(); ++i)
                maxStep = std::max (maxStep, (double) std::abs (y[i] - y[i - 1]));

            const auto steadyStep = 0.8 * 2.0 * kPi * 50.0 / kSampleRate;
            std::printf ("  Largest sample-to-sample step: %.4f (steady 50 Hz sine: %.4f)\n", maxStep, steadyStep);
            check (maxStep < 3.0 * steadyStep, "switching the bypass on and off doesn't click");
        }
    }

    // --- CPU ---------------------------------------------------------------------------
    // Median of five runs per case: a single run on a desktop PC varies by a few percent,
    // and a CPU change only counts if it beats that noise.
    std::printf ("\n== CPU (stereo, 4 s of noise, median of 5 runs) ==\n");
    {
        for (const auto& [sampleRate, blockSize] : { std::pair { 48000.0, 512 }, std::pair { 44100.0, 64 }, std::pair { 96000.0, 128 } })
        {
            auto cpuHost = makeHostAt (sampleRate, blockSize);
            const auto input = noise (0.25, 4.0 * sampleRate / kSampleRate);   // 4 s at this sample rate

            for (const auto& [name, settings] : { std::pair { "Defaults (Mix 100 %, clipper off)    ", Settings { 25.0f, 50.0f, 0.0f, 100.0f, false, false } },
                                                  std::pair { "Everything on (Mix 70 %, clipper, LM)", Settings { 60.0f, 60.0f, 0.0f, 70.0f, true, true } } })
            {
                cpuHost->apply (settings);
                cpuHost->run (noise (0.25, 1.0));   // warm-up
                std::vector<double> percent;

                for (int run = 0; run < 5; ++run)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    cpuHost->run (input);
                    percent.push_back (100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 4.0);
                }

                std::sort (percent.begin(), percent.end());
                std::printf ("  %5.1f kHz / %3d samples  %s: %.2f %% of one core (runs %.2f-%.2f)\n",
                             sampleRate / 1000.0, blockSize, name, percent[2], percent.front(), percent.back());
            }
        }
    }

    std::printf ("\n%s\n", failures == 0 ? "ALL MEASUREMENTS OK" : "SOME MEASUREMENTS FAILED");
    return failures == 0 ? 0 : 1;
}
