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
//==============================================================================
void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    auto* param = apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float getParam (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    auto* param = apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

juce::StringArray userNames (const PresetManager& manager)
{
    juce::StringArray names;

    for (const auto& entry : manager.getEntries())
        if (entry.ref.source == PresetManager::Source::user)
            names.add (entry.ref.name);

    return names;
}

const PresetManager::Entry* findEntry (const PresetManager& manager, PresetManager::Source source, const juce::String& name)
{
    for (const auto& entry : manager.getEntries())
        if (entry.ref.source == source && entry.ref.name.equalsIgnoreCase (name))
            return &entry;

    return nullptr;
}

void resetParams (juce::AudioProcessorValueTreeState& apvts)
{
    setParam (apvts, ParamIDs::fatness, 25.0f);
    setParam (apvts, ParamIDs::color, 50.0f);
    setParam (apvts, ParamIDs::mix, 100.0f);
    setParam (apvts, ParamIDs::output, 0.0f);
    setParam (apvts, ParamIDs::clipper, 0.0f);
    setParam (apvts, ParamIDs::levelMatch, 0.0f);
}

juce::MemoryBlock stateFromXml (const juce::String& xmlText)
{
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (*juce::XmlDocument::parse (xmlText), block);
    return block;
}

juce::ValueTree stateTree (HeftAudioProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    return juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize()));
}

//==============================================================================
void testPresetManager (HeftAudioProcessor& processor, const juce::File& scratch)
{
    using Source = PresetManager::Source;
    auto& apvts = processor.apvts;
    const auto userFolder = scratch.getChildFile ("User presets");
    resetParams (apvts);

    PresetManager manager (apvts, userFolder);
    manager.removeFile = [] (const juce::File& f) { return f.deleteFile(); };   // not the real Recycle Bin
    manager.refreshList();

    section ("Presets: a new instance and the asterisk");
    {
        check (manager.getCurrentName() == "Default" && manager.getCurrent().source == Source::defaultPreset,
               "a new instance starts on Default");
        check (! manager.isModified(), "no asterisk at first");
        check (! userFolder.exists(), "the user folder isn't created until it's needed");
        check (manager.getEntries().size() == (size_t) manager.getNumFactoryEntries() && manager.getEntries()[0].ref.name == "Default",
               "the list starts with Default and has no user presets yet");

        setParam (apvts, ParamIDs::fatness, 60.0f);
        check (manager.isModified(), "moving a knob shows the asterisk");
        setParam (apvts, ParamIDs::fatness, 25.0f);
        check (! manager.isModified(), "moving it back removes it");
        setParam (apvts, ParamIDs::levelMatch, 1.0f);
        check (! manager.isModified(), "Level Match is not part of a preset");
        setParam (apvts, ParamIDs::clipper, 1.0f);
        check (manager.isModified(), "the clipper is");
        setParam (apvts, ParamIDs::clipper, 0.0f);
    }
    endSection();

    section ("Presets: save as, list order, save in place");
    {
        setParam (apvts, ParamIDs::fatness, 70.0f);
        check (manager.saveAs ("Kick 10").wasOk(), "save as Kick 10");
        setParam (apvts, ParamIDs::fatness, 40.0f);
        check (manager.saveAs ("Kick 2").wasOk(), "save as Kick 2");
        check (userFolder.getChildFile ("Kick 2.heftpreset").existsAsFile(), "the file is in the user folder");
        check (userNames (manager) == juce::StringArray { "Kick 2", "Kick 10" }, "natural order: Kick 2 before Kick 10");
        check (manager.getCurrent().source == Source::user && manager.getCurrentName() == "Kick 2" && ! manager.isModified(),
               "the saved preset becomes the current one, without asterisk");
        check (manager.canSaveInPlace(), "a user preset can be saved in place");

        setParam (apvts, ParamIDs::color, 80.0f);
        check (manager.isModified(), "asterisk after a change");
        check (manager.save().wasOk() && ! manager.isModified(), "Save overwrites and clears the asterisk");

        PresetFile::Values onDisk {};
        PresetFile::read (userFolder.getChildFile ("Kick 2.heftpreset"), PresetFile::makeSpecs (apvts), onDisk);
        check (onDisk[0] == 40.0f && onDisk[1] == 80.0f, "the file has the new values");

        const auto invalid = manager.saveAs ("a/b");
        check (invalid.failed() && invalid.getErrorMessage().contains ("can't contain"), "an invalid name gives a reason");
        check (manager.userPresetExists ("kick 2") && ! manager.userPresetExists ("Kick 3"), "exists ignores case, like Windows");
        check (manager.saveAs ("kick 2").wasOk() && userNames (manager).size() == 2, "saving over an existing name (after confirming) doesn't duplicate it");
    }
    endSection();

    section ("Presets: loading, arrows and the list");
    {
        const auto numFactory = manager.getNumFactoryEntries();
        check (manager.load (manager.getEntries()[0]).wasOk() && manager.getCurrentName() == "Default", "load Default");
        check (getParam (apvts, ParamIDs::fatness) == 25.0f && getParam (apvts, ParamIDs::color) == 50.0f, "Default sets the default values");

        check (manager.loadPrevious().wasOk() && manager.getCurrentName() == userNames (manager)[userNames (manager).size() - 1],
               "previous from the first wraps to the last");
        check (manager.loadNext().wasOk() && manager.getCurrentName() == "Default", "next from the last wraps to the first");
        check (manager.loadNext().wasOk() && manager.getEntries()[1].ref.refersTo (manager.getCurrent()), "next goes down the list");
        juce::ignoreUnused (numFactory);

        const auto specs = PresetFile::makeSpecs (apvts);
        PresetFile::write (userFolder.getChildFile ("By hand.heftpreset"), { 10.0f, 20.0f, 30.0f, -6.0f, 1.0f }, specs, "0.4.0");
        manager.refreshList();
        check (userNames (manager).contains ("By hand"), "a file copied into the folder shows up on the next refresh");

        const auto* byHand = findEntry (manager, Source::user, "By hand");
        check (byHand != nullptr && manager.load (*byHand).wasOk() && getParam (apvts, ParamIDs::output) == -6.0f
                   && getParam (apvts, ParamIDs::clipper) == 1.0f,
               "loading a user preset sets its values");

        userFolder.getChildFile ("Broken.heftpreset").replaceWithText ("this is not xml");
        manager.refreshList();
        const auto before = getParam (apvts, ParamIDs::fatness);
        const auto broken = manager.load (*findEntry (manager, Source::user, "Broken"));
        check (broken.failed() && broken.getErrorMessage() == "\"Broken\" is not a Heft preset.", "a corrupt file gives a message");
        check (getParam (apvts, ParamIDs::fatness) == before && manager.getCurrentName() == "By hand", "and changes nothing");
        userFolder.getChildFile ("Broken.heftpreset").deleteFile();

        const PresetManager::Entry gone { { Source::user, "Gone" }, userFolder.getChildFile ("Gone.heftpreset") };
        const auto missing = manager.load (gone);
        check (missing.failed() && missing.getErrorMessage() == "\"Gone\" no longer exists.", "a missing file gives a message");
    }
    endSection();

    section ("Presets: rename and delete");
    {
        manager.load (*findEntry (manager, Source::user, "kick 2"));
        setParam (apvts, ParamIDs::mix, 60.0f);
        check (manager.isModified(), "modified before renaming");

        const auto clash = manager.renameCurrent ("Kick 10");
        check (clash.failed() && clash.getErrorMessage().contains ("already exists"), "renaming never overwrites");
        check (manager.renameCurrent ("Snare").wasOk() && manager.getCurrentName() == "Snare", "rename");
        check (userFolder.getChildFile ("Snare.heftpreset").existsAsFile() && ! userFolder.getChildFile ("kick 2.heftpreset").existsAsFile(),
               "the file was renamed");
        check (manager.isModified(), "renaming keeps the asterisk");
        check (manager.renameCurrent ("SNARE").wasOk()
                   && userFolder.findChildFiles (juce::File::findFiles, false, "SNARE.heftpreset")[0].getFileName() == "SNARE.heftpreset",
               "changing only the case works");

        const auto mixBefore = getParam (apvts, ParamIDs::mix);
        check (manager.deleteCurrent().wasOk() && ! userNames (manager).contains ("SNARE"), "delete removes the file");
        check (manager.getCurrentName() == "Untitled" && ! manager.isModified(), "the current preset becomes Untitled");
        check (getParam (apvts, ParamIDs::mix) == mixBefore, "the sound doesn't change");

        manager.load (manager.getEntries()[0]);
        check (manager.renameCurrent ("X").failed() && manager.deleteCurrent().failed(), "Default can't be renamed or deleted");
    }
    endSection();

    section ("Presets: import");
    {
        const auto outside = scratch.getChildFile ("Downloads");
        outside.createDirectory();
        const auto specs = PresetFile::makeSpecs (apvts);
        PresetFile::write (outside.getChildFile ("Imported.heftpreset"), { 33.0f, 44.0f, 55.0f, -1.0f, 0.0f }, specs, "0.4.0");
        PresetFile::write (outside.getChildFile ("Kick 10.heftpreset"), { 1.0f, 2.0f, 3.0f, 0.0f, 0.0f }, specs, "0.4.0");
        outside.getChildFile ("Bad.heftpreset").replaceWithText ("nope");
        outside.getChildFile ("Newer.heftpreset").replaceWithText ("<HeftPreset formatVersion=\"9\"/>");

        const auto result = manager.importFiles ({ outside.getChildFile ("Imported.heftpreset"), outside.getChildFile ("Bad.heftpreset"),
                                                   outside.getChildFile ("Kick 10.heftpreset"), outside.getChildFile ("Newer.heftpreset") });
        check (result.imported == 2 && result.problems.size() == 2, "two imported, two skipped");
        check (result.summary() == "2 imported, 2 skipped: Bad.heftpreset is not a Heft preset. "
                                   "Newer.heftpreset was made with a newer version of Heft.",
               "one summary message: " + result.summary());
        check (userNames (manager).contains ("Imported") && userNames (manager).contains ("Kick 10 (2)") && userNames (manager).contains ("Kick 10"),
               "a taken name gets \" (2)\", never a silent overwrite");
        check (manager.getCurrentName() == "Imported" && getParam (apvts, ParamIDs::fatness) == 33.0f, "the first imported preset is loaded");

        const auto again = manager.importFiles ({ outside.getChildFile ("Imported.heftpreset") });
        check (again.imported == 1 && userNames (manager).contains ("Imported (2)"), "importing again adds \" (2)\"");
    }
    endSection();

    section ("Presets: names with accents and Japanese");
    {
        const juce::String spanish (juce::CharPointer_UTF8 ("Bajo \xc3\xb1"));
        const juce::String japanese (juce::CharPointer_UTF8 ("\xe3\x83\x89\xe3\x83\xad\xe3\x83\x83\xe3\x83\x97"));
        setParam (apvts, ParamIDs::fatness, 81.0f);
        check (manager.saveAs (spanish).wasOk() && manager.saveAs (japanese).wasOk(), "save");
        check (userNames (manager).contains (spanish) && userNames (manager).contains (japanese), "listed with the right names");
        setParam (apvts, ParamIDs::fatness, 10.0f);
        check (manager.load (*findEntry (manager, Source::user, spanish)).wasOk() && getParam (apvts, ParamIDs::fatness) == 81.0f
                   && manager.getCurrentName() == spanish,
               "load back");
    }
    endSection();

    section ("Presets: a project points at a preset that's gone");
    {
        setParam (apvts, ParamIDs::fatness, 64.0f);
        manager.saveAs ("Will vanish");
        setParam (apvts, ParamIDs::color, 12.0f);
        juce::ValueTree saved ("Parameters");
        manager.writeState (saved);
        userFolder.getChildFile ("Will vanish.heftpreset").deleteFile();

        PresetManager reopened (apvts, userFolder);
        reopened.readState (saved.getChildWithName (PresetManager::abStateType));
        check (reopened.getCurrentName() == "Will vanish" && reopened.isModified(), "the name stays, the asterisk comes from the saved snapshot");
        check (reopened.save().wasOk() && userFolder.getChildFile ("Will vanish.heftpreset").existsAsFile(), "Save writes the file again");
    }
    endSection();

    resetParams (apvts);
}

//==============================================================================
void testProjectState()
{
    section ("Project state: save and reopen");
    {
        HeftAudioProcessor original;
        setParam (original.apvts, ParamIDs::fatness, 66.0f);
        setParam (original.apvts, ParamIDs::levelMatch, 1.0f);
        original.editorWidth = 900;

        juce::MemoryBlock state;
        original.getStateInformation (state);

        HeftAudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        check (getParam (reopened.apvts, ParamIDs::fatness) == 66.0f && getParam (reopened.apvts, ParamIDs::levelMatch) == 1.0f,
               "parameters come back");
        check (reopened.presetManager.getCurrentName() == "Default" && reopened.presetManager.isModified(), "preset and asterisk come back");
        check (reopened.editorWidth.load() == 900, "the window size comes back");

        const auto tree = stateTree (reopened);
        check ((int) tree[PresetManager::stateVersionId] == 2 && tree.getChildWithName (PresetManager::abStateType).isValid(),
               "the saved state has stateVersion 2 and the A/B data");
        check (tree.getNumChildren() == 7, "one ABState child plus the six parameters, no duplicates after reopening");
    }
    endSection();

    section ("Project state: projects saved with 0.3.1 open the same");
    {
        // What 0.3.1 saved: the APVTS tree with its PARAMs plus editorWidth, and nothing else.
        const auto old = stateFromXml ("<Parameters editorWidth=\"760\">"
                                       "<PARAM id=\"fatness\" value=\"62.0\"/><PARAM id=\"color\" value=\"70.0\"/>"
                                       "<PARAM id=\"output\" value=\"-2.5\"/><PARAM id=\"clipper\" value=\"1.0\"/>"
                                       "<PARAM id=\"levelMatch\" value=\"1.0\"/><PARAM id=\"mix\" value=\"80.0\"/></Parameters>");
        HeftAudioProcessor processor;
        processor.setStateInformation (old.getData(), (int) old.getSize());
        auto& apvts = processor.apvts;
        check (getParam (apvts, ParamIDs::fatness) == 62.0f && getParam (apvts, ParamIDs::color) == 70.0f
                   && getParam (apvts, ParamIDs::output) == -2.5f && getParam (apvts, ParamIDs::clipper) == 1.0f
                   && getParam (apvts, ParamIDs::levelMatch) == 1.0f && getParam (apvts, ParamIDs::mix) == 80.0f,
               "same knobs");
        check (processor.presetManager.getCurrentName() == "Untitled" && ! processor.presetManager.isModified(), "shown as Untitled");
        check (processor.presetManager.getActiveSlot() == PresetManager::Slot::a, "A is active");
        check (processor.editorWidth.load() == 760, "window size kept");

        const auto atDefaults = stateFromXml ("<Parameters editorWidth=\"0\">"
                                              "<PARAM id=\"fatness\" value=\"25.0\"/><PARAM id=\"color\" value=\"50.0\"/>"
                                              "<PARAM id=\"output\" value=\"0.0\"/><PARAM id=\"clipper\" value=\"0.0\"/>"
                                              "<PARAM id=\"levelMatch\" value=\"0.0\"/><PARAM id=\"mix\" value=\"100.0\"/></Parameters>");
        HeftAudioProcessor fresh;
        fresh.setStateInformation (atDefaults.getData(), (int) atDefaults.getSize());
        check (fresh.presetManager.getCurrentName() == "Default" && ! fresh.presetManager.isModified(), "at the defaults it shows Default");
    }
    endSection();

    section ("Project state: odd data never crashes or resets anything");
    {
        const auto odd = stateFromXml ("<Parameters stateVersion=\"2\">"
                                       "<PARAM id=\"fatness\" value=\"44.0\"/><PARAM id=\"color\" value=\"50.0\"/>"
                                       "<PARAM id=\"output\" value=\"0.0\"/><PARAM id=\"clipper\" value=\"0.0\"/>"
                                       "<PARAM id=\"levelMatch\" value=\"0.0\"/><PARAM id=\"mix\" value=\"100.0\"/>"
                                       "<ABState active=\"C\"><SlotA presetSource=\"bogus\" presetName=\"X\"/>"
                                       "<SlotB fatness=\"999\" color=\"abc\" presetSource=\"user\"/></ABState></Parameters>");
        HeftAudioProcessor processor;
        processor.setStateInformation (odd.getData(), (int) odd.getSize());
        check (getParam (processor.apvts, ParamIDs::fatness) == 44.0f, "the parameters are untouched");
        check (processor.presetManager.getActiveSlot() == PresetManager::Slot::a && processor.presetManager.getCurrentName() == "Untitled",
               "unknown values fall back to A and Untitled");

        processor.presetManager.switchTo (PresetManager::Slot::b);
        check (getParam (processor.apvts, ParamIDs::fatness) == 100.0f && getParam (processor.apvts, ParamIDs::color) == 50.0f,
               "slot B's values were clamped or defaulted");

        const auto garbage = juce::String ("definitely not a plugin state");
        processor.setStateInformation (garbage.toRawUTF8(), (int) garbage.getNumBytesAsUTF8());
        check (getParam (processor.apvts, ParamIDs::fatness) == 100.0f, "garbage is ignored");

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < 500; ++i)
            processor.setStateInformation (state.getData(), (int) state.getSize());

        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        check (ms < 2000.0, "500 setStateInformation calls in a row are quick (" + juce::String (ms, 0) + " ms)");
    }
    endSection();
}

//==============================================================================
void testABComparator (const juce::File& scratch)
{
    using Slot = PresetManager::Slot;
    HeftAudioProcessor processor;
    auto& apvts = processor.apvts;
    PresetManager manager (apvts, scratch.getChildFile ("AB presets"));
    manager.removeFile = [] (const juce::File& f) { return f.deleteFile(); };

    section ("A/B: each slot keeps its settings, preset and asterisk");
    {
        check (manager.getActiveSlot() == Slot::a, "A is active at first");

        setParam (apvts, ParamIDs::fatness, 70.0f);
        check (manager.getCurrentName() == "Default" && manager.isModified(), "A: Default *");

        const auto before = manager.getChangeCount();
        manager.switchTo (Slot::a);
        check (manager.getChangeCount() == before && getParam (apvts, ParamIDs::fatness) == 70.0f,
               "clicking the active letter does nothing");

        manager.switchTo (Slot::b);
        check (manager.getActiveSlot() == Slot::b && getParam (apvts, ParamIDs::fatness) == 25.0f, "B has its own values");
        check (manager.getCurrentName() == "Default" && ! manager.isModified(), "B: Default, no asterisk");

        setParam (apvts, ParamIDs::fatness, 40.0f);
        manager.saveAs ("Bass");
        setParam (apvts, ParamIDs::color, 90.0f);
        check (manager.getCurrentName() == "Bass" && manager.isModified(), "B: Bass *");

        manager.switchTo (Slot::a);
        check (getParam (apvts, ParamIDs::fatness) == 70.0f && getParam (apvts, ParamIDs::color) == 50.0f,
               "back to A restores its knobs");
        check (manager.getCurrentName() == "Default" && manager.isModified(), "and its preset and asterisk");

        manager.switchTo (Slot::b);
        check (getParam (apvts, ParamIDs::color) == 90.0f && manager.getCurrentName() == "Bass" && manager.isModified(),
               "and B remembered its unsaved change");
    }
    endSection();

    section ("A/B: Level Match stays as it is");
    {
        setParam (apvts, ParamIDs::levelMatch, 1.0f);
        manager.switchTo (Slot::a);
        check (getParam (apvts, ParamIDs::levelMatch) == 1.0f, "on in B, still on after switching to A");
        setParam (apvts, ParamIDs::levelMatch, 0.0f);
        manager.switchTo (Slot::b);
        check (getParam (apvts, ParamIDs::levelMatch) == 0.0f, "off stays off");
    }
    endSection();

    section ("A/B: Copy and presets only touch the active slot");
    {
        // B is active: "Bass *" with color 90.
        manager.copyActiveToOther();
        manager.switchTo (Slot::a);
        check (getParam (apvts, ParamIDs::fatness) == 40.0f && getParam (apvts, ParamIDs::color) == 90.0f,
               "Copy B to A copied the values");
        check (manager.getCurrentName() == "Bass" && manager.isModified(), "and the preset with its asterisk");

        manager.load (manager.getEntries()[0]);   // Default, into A
        manager.switchTo (Slot::b);
        check (manager.getCurrentName() == "Bass" && getParam (apvts, ParamIDs::color) == 90.0f, "loading in A left B alone");

        manager.save();   // saves Bass from B
        manager.switchTo (Slot::a);
        check (manager.getCurrentName() == "Default" && ! manager.isModified(), "saving in B left A alone");
    }
    endSection();

    section ("A/B: saved with the project");
    {
        // A: Default (clean). Make B different and active, then save the project.
        manager.switchTo (Slot::b);
        setParam (apvts, ParamIDs::mix, 35.0f);

        juce::ValueTree saved ("Parameters");
        manager.writeState (saved);
        const auto ab = saved.getChildWithName (PresetManager::abStateType);
        check (ab["active"].toString() == "B" && (float) ab.getChildWithName ("SlotB")["mix"] == 35.0f,
               "the active slot is written with the current knobs");

        // Reopen: the parameters come back first (as the host does), then the A/B data.
        // The processor's parameters (B's knobs right now) plus this manager's A/B data.
        auto tree = stateTree (processor);
        tree.removeChild (tree.getChildWithName (PresetManager::abStateType), nullptr);
        tree.appendChild (ab.createCopy(), nullptr);
        juce::MemoryBlock withAB;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), withAB);

        HeftAudioProcessor reopened;
        reopened.setStateInformation (withAB.getData(), (int) withAB.getSize());
        auto& m = reopened.presetManager;
        check (m.getActiveSlot() == Slot::b && m.getCurrentName() == "Bass" && m.isModified(), "B active, Bass *");
        check (getParam (reopened.apvts, ParamIDs::mix) == 35.0f, "B's knobs");

        m.switchTo (Slot::a);
        check (m.getCurrentName() == "Default" && ! m.isModified() && getParam (reopened.apvts, ParamIDs::mix) == 100.0f,
               "and A comes back as it was");
    }
    endSection();
}

//==============================================================================
void testFactoryPresets (const juce::File& scratch)
{
    using Source = PresetManager::Source;
    HeftAudioProcessor processor;
    auto& apvts = processor.apvts;
    PresetManager manager (apvts, scratch.getChildFile ("Factory test"));
    manager.removeFile = [] (const juce::File& f) { return f.deleteFile(); };
    manager.refreshList();

    section ("Factory presets: built into the plugin");
    {
        check (manager.getNumFactoryEntries() == 1 + HEFT_FACTORY_PRESET_COUNT,
               "every file in Presets/Factory parses and is listed (" + juce::String (HEFT_FACTORY_PRESET_COUNT) + " files)");
        check (manager.getEntries()[0].ref.source == Source::defaultPreset && manager.getEntries()[0].ref.name == "Default",
               "Default comes first");

       #if HEFT_HAS_FACTORY_PRESETS
        const auto* parallel = findEntry (manager, Source::factory, "Parallel");
        check (parallel != nullptr, "\"01 Parallel\" is listed as \"Parallel\" (the number orders, it isn't shown)");

        if (parallel != nullptr)
        {
            const auto entry = *parallel;
            check (manager.load (entry).wasOk() && getParam (apvts, ParamIDs::fatness) == 70.0f && getParam (apvts, ParamIDs::mix) == 50.0f,
                   "loads its values");
            check (manager.getCurrentName() == "Parallel" && ! manager.isModified(), "becomes the current preset, no asterisk");
            check (! manager.canSaveInPlace() && manager.save().failed(), "Save doesn't overwrite it (the bar turns it into Save as)");
            check (manager.renameCurrent ("X").failed() && manager.deleteCurrent().failed(), "it can't be renamed or deleted");

            check (manager.saveAs ("Parallel").wasOk(), "a user preset can have the same name");
            check (findEntry (manager, Source::user, "Parallel") != nullptr && findEntry (manager, Source::factory, "Parallel") != nullptr,
                   "both are listed");
            check (manager.getCurrent().source == Source::user && ! manager.getCurrent().refersTo (entry.ref),
                   "and they're told apart by where they come from");
        }
       #endif
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
        testPresetManager (processor, scratch.folder);
    }

    testProjectState();
    testABComparator (scratch.folder);
    testFactoryPresets (scratch.folder);

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
