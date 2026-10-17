// HeftTests: unit tests for the parts of Heft that aren't audio: preset files, the
// preset manager, the A/B comparator and the state saved with projects. It runs the
// code directly (no host, no window) and works in a temporary folder, never in
// Documents\Heft\Presets.
//
// Usage: HeftTests.exe   (exit code 0 = everything passed)

#include "PluginProcessor.h"
#include "Presets/PresetFile.h"

#include <clocale>
#include <cstdio>

namespace
{
int failures = 0, sectionChecks = 0, sectionFailures = 0;
const char* currentSection = "";

void section (const char* name)
{
    currentSection = name;
    sectionChecks = sectionFailures = 0;
}

void endSection()
{
    std::printf ("  [%s] %s (%d checks)\n", sectionFailures == 0 ? " OK " : "FAIL", currentSection, sectionChecks);
}

void check (bool ok, const juce::String& what)
{
    ++sectionChecks;

    if (! ok)
    {
        ++failures;
        ++sectionFailures;
        std::printf ("         failed: %s\n", what.toRawUTF8());
    }
}

// A scratch folder under %TEMP% that is deleted at the end.
struct TempFolder
{
    TempFolder()
    {
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("HeftTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        folder.createDirectory();
    }

    ~TempFolder() { folder.deleteRecursively(); }

    juce::File folder;
};

//==============================================================================
void testPresetFile (HeftAudioProcessor& processor, const juce::File& scratch)
{
    using namespace PresetFile;
    const auto specs = makeSpecs (processor.apvts);

    section ("Preset parameters come from Parameters.h");
    {
        check (specs[0].id == "fatness" && specs[1].id == "color" && specs[2].id == "mix"
                   && specs[3].id == "output" && specs[4].id == "clipper",
               "ids in presetParamIds order");
        const auto d = defaults (specs);
        check (d[0] == 25.0f && d[1] == 50.0f && d[2] == 100.0f && d[3] == 0.0f && d[4] == 0.0f,
               "defaults are 25 / 50 / 100 / 0 dB / off");
        check (specs[4].isSwitch && ! specs[0].isSwitch, "clipper is the only switch");
        check (specs[3].minValue == -24.0f && specs[3].maxValue == 12.0f, "output range -24..+12");
    }
    endSection();

    section ("File format: write and read back");
    {
        const Values v { 62.0f, 30.5f, 80.0f, -3.0f, 1.0f };
        const auto xml = toXml (v, specs, "0.4.0");
        Values back {};
        check (fromXml (xml, specs, back).wasOk() && back == v, "round trip keeps every value");
        check (xml.contains ("<HeftPreset") && xml.contains ("formatVersion=\"1\"") && xml.contains ("pluginVersion=\"0.4.0\""),
               "root, formatVersion and pluginVersion");
        check (xml.contains ("value=\"62.0\"") && xml.contains ("value=\"-3.0\"") && xml.contains ("value=\"1\""),
               "values in real units: " + xml);
        check (! xml.contains ("name="), "the preset name is not stored inside the file");

        // Spanish regional settings use a decimal comma; files must still use a dot.
        const auto* previous = std::setlocale (LC_ALL, nullptr);
        const juce::String saved (previous != nullptr ? previous : "C");

        if (std::setlocale (LC_ALL, "es-ES") != nullptr)
        {
            check (formatValue (specs[0], 62.5f) == "62.5", "decimal point even with a comma locale");
            Values parsed {};
            check (fromXml (toXml (v, specs, "0.4.0"), specs, parsed).wasOk() && parsed == v, "round trip with a comma locale");
        }

        std::setlocale (LC_ALL, saved.toRawUTF8());
    }
    endSection();

    section ("File format: lenient where it's safe");
    {
        Values v {};
        const auto partial = "<HeftPreset formatVersion=\"1\"><Param id=\"color\" value=\"70.0\"/>"
                             "<Param id=\"somethingNew\" value=\"5\"/></HeftPreset>";
        check (fromXml (partial, specs, v).wasOk(), "unknown ids are ignored");
        check (v[0] == 25.0f && v[1] == 70.0f && v[2] == 100.0f && v[3] == 0.0f && v[4] == 0.0f,
               "missing parameters take their default");

        const auto outOfRange = "<HeftPreset formatVersion=\"1\"><Param id=\"fatness\" value=\"150\"/>"
                                "<Param id=\"output\" value=\"-80\"/><Param id=\"clipper\" value=\"0.7\"/></HeftPreset>";
        check (fromXml (outOfRange, specs, v).wasOk() && v[0] == 100.0f && v[3] == -24.0f && v[4] == 1.0f,
               "out-of-range values are clamped; clipper is on at 0.5 or more");

        check (fromXml ("<HeftPreset><Param id=\"mix\" value=\"40\"/></HeftPreset>", specs, v).wasOk() && v[2] == 40.0f,
               "a missing formatVersion is read as version 1");
    }
    endSection();

    section ("File format: rejects what isn't a preset");
    {
        Values v { 1.0f, 2.0f, 3.0f, 4.0f, 0.0f };
        const auto before = v;
        const auto wrongRoot = fromXml ("<Something/>", specs, v);
        const auto garbage = fromXml ("not xml at all", specs, v);
        const auto newer = fromXml ("<HeftPreset formatVersion=\"2\"/>", specs, v);
        check (wrongRoot.failed() && wrongRoot.getErrorMessage() == "is not a Heft preset.", "wrong root element");
        check (garbage.failed(), "unreadable text");
        check (newer.failed() && newer.getErrorMessage() == "was made with a newer version of Heft.", "newer formatVersion");
        check (v == before, "a rejected file changes nothing");
    }
    endSection();

    section ("Files on disk: atomic write, size limit, Unicode names");
    {
        const Values v { 70.0f, 55.0f, 50.0f, 1.5f, 0.0f };

        for (auto name : { juce::String ("Kick gordo"), juce::String (juce::CharPointer_UTF8 ("Bajo \xc3\xb1")),
                           juce::String (juce::CharPointer_UTF8 ("\xe3\x83\x89\xe3\x83\xad\xe3\x83\x83\xe3\x83\x97")) })
        {
            const auto file = scratch.getChildFile (name + extension);
            Values back {};
            check (write (file, v, specs, "0.4.0").wasOk() && file.existsAsFile(), "write " + name);
            check (read (file, specs, back).wasOk() && back == v, "read back " + name);
        }

        const auto missing = read (scratch.getChildFile ("Nope" + extension), specs, *std::make_unique<Values>());
        check (missing.failed() && missing.getErrorMessage() == "no longer exists.", "missing file");

        const auto big = scratch.getChildFile ("Big" + extension);
        big.replaceWithText ("<HeftPreset>" + juce::String::repeatedString ("<!-- padding -->", 5000) + "</HeftPreset>");
        Values ignored {};
        check (read (big, specs, ignored).failed(), "files over 64 KB are refused without parsing");

        const auto locked = scratch.getChildFile ("Locked" + extension);
        write (locked, v, specs, "0.4.0");
        locked.setReadOnly (true);
        const auto lockedResult = write (locked, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, specs, "0.4.0");
        check (lockedResult.failed() && lockedResult.getErrorMessage().contains (locked.getFullPathName()),
               "writing over a read-only file fails with the path in the message");
        Values stillThere {};
        check (read (locked, specs, stillThere).wasOk() && stillThere == v, "the read-only file is left intact");
        locked.setReadOnly (false);

        check (scratch.findChildFiles (juce::File::findFiles, false, "*.tmp").isEmpty(), "no temporary files left behind");
    }
    endSection();

    section ("Preset names");
    {
        check (validateName ("Kick 2").isEmpty(), "a normal name");
        check (validateName (juce::CharPointer_UTF8 ("Bajo \xc3\xb1")).isEmpty(), "accents");
        check (validateName (juce::CharPointer_UTF8 ("\xe3\x83\x89\xe3\x83\xad\xe3\x83\x83\xe3\x83\x97")).isEmpty(), "Japanese");
        check (validateName (juce::String::repeatedString ("a", 64)).isEmpty(), "64 characters is fine");
        check (validateName ("v1.2 test").isEmpty(), "a dot in the middle is fine");

        for (auto bad : { "", " Kick", "Kick ", "a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b",
                          "Kick.", "CON", "con", "Nul", "COM1", "LPT9", "AUX.mine" })
            check (validateName (bad).isNotEmpty(), "rejects \"" + juce::String (bad) + "\"");

        check (validateName (juce::String::repeatedString ("a", 65)).isNotEmpty(), "65 characters is too long");
    }
    endSection();

    section ("Factory display names and the modified tolerance");
    {
        check (factoryDisplayName ("01 Kick gordo.heftpreset") == "Kick gordo", "\"01 \" prefix hidden");
        check (factoryDisplayName ("02-Bass.heftpreset") == "Bass", "\"02-\" prefix hidden");
        check (factoryDisplayName ("10 - Drum bus.heftpreset") == "Drum bus", "\"10 - \" prefix hidden");
        check (factoryDisplayName ("Plain.heftpreset") == "Plain", "no prefix");
        check (factoryDisplayName ("808.heftpreset") == "808", "a number alone is kept");

        const Values a { 50.0f, 50.0f, 50.0f, 0.0f, 0.0f };
        auto b = a;
        b[0] = 50.04f;
        check (matches (a, b, specs), "within half a step");
        b[0] = 50.1f;
        check (! matches (a, b, specs), "one step away");
        b = a;
        b[4] = 1.0f;
        check (! matches (a, b, specs), "clipper changed");
    }
    endSection();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempFolder scratch;

    std::printf ("\n== HeftTests ==\n");

    {
        HeftAudioProcessor processor;
        testPresetFile (processor, scratch.folder);
    }

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
