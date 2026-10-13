// HeftMeasure: loads Heft.vst3 the way a DAW would, feeds it test signals
// and measures the result. Use it to check that changes to Tuning.h don't
// break anything (transparency, latency, clipper ceiling, level match).
//
// Usage:  HeftMeasure.exe "path\to\Heft.vst3"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr double kPi         = juce::MathConstants<double>::pi;

struct Settings
{
    float fatness = 0.0f, color = 50.0f, outputDb = 0.0f;
    bool  clipper = false, levelMatch = false;
};

class Host
{
public:
    explicit Host (std::unique_ptr<juce::AudioPluginInstance> p) : plugin (std::move (p))
    {
        for (auto* param : plugin->getParameters())
            params[param->getName (64)] = param;

        plugin->prepareToPlay (kSampleRate, kBlockSize);
    }

    ~Host() { plugin->releaseResources(); }

    bool apply (const Settings& s)
    {
        return set ("Fatness", s.fatness / 100.0f)
            && set ("Color", s.color / 100.0f)
            && set ("Output", (s.outputDb + 24.0f) / 36.0f)
            && set ("Clipper", s.clipper ? 1.0f : 0.0f)
            && set ("Level Match", s.levelMatch ? 1.0f : 0.0f);
    }

    // Same signal on both channels. Returns the processed left channel.
    std::vector<float> run (const std::vector<float>& input)
    {
        std::vector<float> output (input.size());
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;

        for (size_t start = 0; start < input.size(); start += kBlockSize)
        {
            const auto n = (int) std::min<size_t> (kBlockSize, input.size() - start);
            buffer.setSize (2, n, false, false, true);

            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, input.data() + start, n);

            plugin->processBlock (buffer, midi);

            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, output.begin() + (long) start);
        }

        return output;
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
    std::map<juce::String, juce::AudioProcessorParameter*> params;
};

std::vector<float> sine (double freq, double amplitude, double seconds)
{
    std::vector<float> s ((size_t) (seconds * kSampleRate));

    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (float) (amplitude * std::sin (2.0 * kPi * freq * (double) i / kSampleRate));

    return s;
}

// Kick-like hits (decaying 60 Hz sine) every 250 ms, peaking at "peak".
std::vector<float> drumHits (double peak, double seconds)
{
    std::vector<float> s ((size_t) (seconds * kSampleRate));
    const auto period = (size_t) (0.25 * kSampleRate);

    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto t = (double) (i % period) / kSampleRate;
        s[i] = (float) (peak * std::exp (-t / 0.06) * std::sin (2.0 * kPi * 60.0 * t));
    }

    return s;
}

struct Analysis
{
    double fundamental = 0, thdnDb = 0, h2Db = 0, h3Db = 0, peak = 0, dc = 0;
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

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    failures += ok ? 0 : 1;
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
    auto instance = format.createInstanceFromDescription (*types[0], kSampleRate, kBlockSize, error);

    if (instance == nullptr)
    {
        std::printf ("ERROR loading the plugin: %s\n", error.toRawUTF8());
        return 2;
    }

    Host host (std::move (instance));
    const auto amp = 0.5;   // -6 dBFS peak

    // --- Latency ---------------------------------------------------------------
    std::printf ("\n== Latency ==\n");
    {
        if (! host.apply ({ 0.0f, 50.0f, 0.0f, false, false }))
            return 2;

        host.run (std::vector<float> ((size_t) kSampleRate, 0.0f));   // let the smoothing settle

        std::vector<float> impulse ((size_t) kSampleRate / 2, 0.0f);
        const size_t at = 1000;
        impulse[at] = 0.5f;
        const auto y = host.run (impulse);

        size_t peakIndex = 0;
        for (size_t i = 0; i < y.size(); ++i)
            if (std::abs (y[i]) > std::abs (y[peakIndex]))
                peakIndex = i;

        const auto measured = (int) peakIndex - (int) at;
        std::printf ("  Reported to the host: %d samples (%.2f ms at 48 kHz). Measured with an impulse: %d\n",
                     host.latency(), 1000.0 * host.latency() / kSampleRate, measured);
        check (std::abs (measured - host.latency()) <= 1, "reported latency matches the real one");
    }

    // --- Transparency at Fatness 0 ------------------------------------------
    std::printf ("\n== Fatness 0 %% (must be transparent) ==\n");
    {
        bool allOk = true;

        for (auto color : { 0.0f, 50.0f, 100.0f })
        {
            for (auto freq : { 100.0, 1000.0, 10000.0 })
            {
                host.apply ({ 0.0f, color, 0.0f, false, false });
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
            host.apply ({ 0.0f, 50.0f, 0.0f, false, false });
            const auto a = analyse (host.run (sine (freq, amp, 2.0)), freq);
            std::printf ("  %.0f kHz %+.2f dB", freq / 1000.0, 20.0 * std::log10 (a.fundamental / amp));
        }

        std::printf ("\n");
    }

    // --- Character ---------------------------------------------------------------
    std::printf ("\n== Character with a 1 kHz sine at -6 dBFS ==\n");
    std::printf ("  Fatness  Color   Level dB   THD+N dB   H2 dB    H3 dB    Peak    DC\n");
    {
        bool allFinite = true;

        for (auto fatness : { 25.0f, 50.0f, 100.0f })
        {
            for (auto color : { 0.0f, 50.0f, 100.0f })
            {
                host.apply ({ fatness, color, 0.0f, false, false });
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
            host.apply ({ fatness, 50.0f, 0.0f, false, false });
            const auto y = host.run (input);
            const auto a = analyse (y, 60.0);
            std::printf ("  Fatness %3.0f%%: RMS %+6.2f dB, peak %+6.2f dB\n",
                         fatness, rmsDb (y) - inRms, 20.0 * std::log10 (a.peak / amp));
        }
    }

    // --- Clipper -----------------------------------------------------------------------
    std::printf ("\n== Clipper ==\n");
    {
        host.apply ({ 50.0f, 50.0f, 12.0f, true, false });
        const auto a = analyse (host.run (sine (200.0, 0.9, 2.0)), 200.0);
        std::printf ("  Fatness 50 %%, Output +12 dB, input at -0.9 dBFS: output peak %.4f\n", a.peak);
        check (a.peak <= 1.0, "the peak never exceeds 0 dBFS with the clipper on");
    }

    // --- Level Match -----------------------------------------------------------------------
    std::printf ("\n== Level Match ==\n");
    {
        bool allOk = true;

        for (auto [fatness, color, freq, level] : { std::tuple { 100.0f, 100.0f, 1000.0, 0.5 },
                                                    std::tuple { 100.0f, 0.0f, 100.0, 0.5 },
                                                    std::tuple { 60.0f, 50.0f, 440.0, 0.1 } })
        {
            const auto input = sine (freq, level, 4.0);

            host.apply ({ fatness, color, 0.0f, false, false });
            const auto off = rmsDb (host.run (input)) - rmsDb (input);

            host.apply ({ fatness, color, 0.0f, false, true });
            const auto on = rmsDb (host.run (input)) - rmsDb (input);

            std::printf ("  Fatness %3.0f%% Color %3.0f%% %5.0f Hz: level change unmatched %+6.2f dB, matched %+6.2f dB\n",
                         fatness, color, freq, off, on);
            allOk = allOk && std::abs (on) < 0.5;
        }

        check (allOk, "with Level Match the difference stays below ±0.5 dB");
    }

    std::printf ("\n%s\n", failures == 0 ? "ALL MEASUREMENTS OK" : "SOME MEASUREMENTS FAILED");
    return failures == 0 ? 0 : 1;
}
