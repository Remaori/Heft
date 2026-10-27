// HeftTests: unit tests for the parts of Heft that aren't audio: preset files, the
// preset manager, the A/B comparator and the state saved with projects, plus the rule
// that processing never allocates memory. It runs the code directly (no host, no
// window) and works in a temporary folder, never in Documents\Heft\Presets.
//
// Usage: HeftTests.exe   (exit code 0 = everything passed)

#include "PluginProcessor.h"
#include "Presets/PresetFile.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// Every operator new of the program goes through here (new[], nothrow and sized delete
// forward to these two), so a test can count what the audio code allocates. Only the
// thread that turns counting on is counted.
// shortcut: juce::HeapBlock (AudioBuffer::setSize, juce::Array) calls malloc directly and
// isn't counted; hook the CRT heap if one of those ever moves into processBlock.
static thread_local bool countingAllocations = false;
static thread_local int allocationCount = 0;

void* operator new (std::size_t size)
{
    if (countingAllocations)
        ++allocationCount;

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }

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

        const auto odd = "<HeftPreset formatVersion=\"1\"><Param id=\"fatness\" value=\"abc\"/>"
                         "<Param id=\"color\" value=\"62,5\"/><Param id=\"output\" value=\" -3.5 \"/></HeftPreset>";
        check (fromXml (odd, specs, v).wasOk() && v[0] == 25.0f && v[1] == 50.0f && v[3] == -3.5f,
               "text that isn't a plain number takes the default, like in project state");
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
        check (scratch.findChildFiles (juce::File::findFiles, false, "*" + tempExtension).isEmpty(),
               "a failed write leaves no temporary file");
        locked.setReadOnly (false);

        check (scratch.findChildFiles (juce::File::findFiles, false, "*" + tempExtension).isEmpty()
                   && scratch.findChildFiles (juce::File::findFiles, false, "*.tmp").isEmpty(),
               "successful writes leave no temporary files");
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

        outside.getChildFile ("Edited.heftpreset")
            .replaceWithText ("<HeftPreset formatVersion=\"1\"><Param id=\"fatness\" value=\"150\"/><Junk/></HeftPreset>");
        manager.importFiles ({ outside.getChildFile ("Edited.heftpreset") });
        const auto imported = userFolder.getChildFile ("Edited.heftpreset").loadFileAsString();
        check (imported.contains ("id=\"fatness\" value=\"100.0\"") && ! imported.contains ("Junk"),
               "the imported file is written by Heft with the values it read, not copied as it was");
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

    section ("Project state: a preset name can't point outside the presets folder");
    {
        // A shared project could carry any text as the user preset's name.
        const auto shared = stateFromXml ("<Parameters stateVersion=\"2\">"
                                          "<PARAM id=\"fatness\" value=\"40.0\"/><PARAM id=\"color\" value=\"50.0\"/>"
                                          "<PARAM id=\"output\" value=\"0.0\"/><PARAM id=\"clipper\" value=\"0.0\"/>"
                                          "<PARAM id=\"levelMatch\" value=\"0.0\"/><PARAM id=\"mix\" value=\"100.0\"/>"
                                          "<ABState active=\"A\">"
                                          "<SlotA presetSource=\"user\" presetName=\"..\\..\\Desktop\\x\"/>"
                                          "<SlotB presetSource=\"user\" presetName=\"C:\\Temp\\x\"/></ABState></Parameters>");
        HeftAudioProcessor processor;
        processor.setStateInformation (shared.getData(), (int) shared.getSize());
        auto& m = processor.presetManager;
        check (m.getCurrentName() == "Untitled" && ! m.canSaveInPlace(), "a relative path is dropped: Untitled, nothing to save in place");
        m.switchTo (PresetManager::Slot::b);
        check (m.getCurrentName() == "Untitled" && ! m.canSaveInPlace(), "an absolute path is dropped too");
        check (m.renameCurrent ("y").failed() && m.deleteCurrent().failed(), "so Rename and Delete have no file to touch");

        const auto normal = stateFromXml ("<Parameters stateVersion=\"2\"><PARAM id=\"fatness\" value=\"40.0\"/>"
                                          "<ABState active=\"A\"><SlotA presetSource=\"user\" presetName=\"Kick 2\"/></ABState></Parameters>");
        HeftAudioProcessor other;
        other.setStateInformation (normal.getData(), (int) normal.getSize());
        check (other.presetManager.getCurrentName() == "Kick 2", "a normal name is kept");

        // Longer than Save as allows, but Heft makes such names itself when it recovers a preset.
        const auto longName = juce::String::repeatedString ("x", 64) + " (recovered)";
        const auto recovered = stateFromXml ("<Parameters stateVersion=\"2\"><PARAM id=\"fatness\" value=\"40.0\"/>"
                                             "<ABState active=\"A\"><SlotA presetSource=\"user\" presetName=\"" + longName
                                             + "\"/></ABState></Parameters>");
        HeftAudioProcessor third;
        third.setStateInformation (recovered.getData(), (int) recovered.getSize());
        check (third.presetManager.getCurrentName() == longName, "a name longer than 64 characters is kept");
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

        manager.switchTo (Slot::a);
        check (manager.getActiveSlot() == Slot::a && getParam (apvts, ParamIDs::fatness) == 70.0f && manager.isModified(),
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

//==============================================================================
void testFolderAvailability (HeftAudioProcessor& processor, const juce::File& scratch)
{
    section ("Folder availability is cached (the UI timer never touches the disk)");
    {
        const auto documents = scratch.getChildFile ("Documents that don't exist yet");
        PresetManager manager (processor.apvts, documents.getChildFile ("Heft").getChildFile ("Presets"));
        check (! manager.isUserFolderAvailable(), "false when Documents (the grandparent) doesn't exist");

        documents.createDirectory();
        check (! manager.isUserFolderAvailable(), "still false right after Documents appears: no disk check");

        manager.refreshList();
        check (manager.isUserFolderAvailable(), "true after refreshList()");
    }
    endSection();
}

//==============================================================================
void testTemporaryFiles (HeftAudioProcessor& processor, const juce::File& scratch)
{
    section ("Temporary files of a save never show up as presets");
    {
        const auto folder = scratch.getChildFile ("Temp files");
        PresetManager manager (processor.apvts, folder);

        check (manager.saveAs ("Kick").wasOk()
                   && folder.findChildFiles (juce::File::findFiles, false, "*").size() == 1,
               "a successful save leaves only the preset");

        // Leftovers of interrupted saves: one old, one recent, plus files that must be left alone.
        const auto now = juce::Time::getCurrentTime();
        const auto old = folder.getChildFile (".Kick.0123456789abcdef.heftpreset-tmp");
        const auto recent = folder.getChildFile (".Snare.fedcba9876543210.heftpreset-tmp");
        const auto notes = folder.getChildFile ("notes.txt");
        const auto backup = folder.getChildFile ("Kick.heftpreset-tmp.bak");

        for (const auto& file : { old, recent, notes, backup })
            file.replaceWithText ("<HeftPreset/>");

        for (const auto& file : { old, notes, backup })
            file.setLastModificationTime (now - juce::RelativeTime::minutes (11));

        manager.refreshList();
        check (! old.exists(), "a temporary file older than 10 minutes is deleted");
        check (recent.exists(), "a recent one is kept (another Heft may be writing it)");
        check (notes.exists() && backup.exists(), "nothing else is ever deleted");
        check (userNames (manager) == juce::StringArray { "Kick" }, "temporary files are never listed");
    }
    endSection();
}

//==============================================================================
void testSaveAsCase (HeftAudioProcessor& processor, const juce::File& scratch)
{
    section ("Save as over a name that differs only in case");
    {
        auto& apvts = processor.apvts;
        const auto folder = scratch.getChildFile ("Case");
        PresetManager manager (apvts, folder);

        setParam (apvts, ParamIDs::fatness, 30.0f);
        manager.saveAs ("kick");
        setParam (apvts, ParamIDs::fatness, 60.0f);
        check (manager.userPresetExists ("Kick"), "\"kick\" already exists");

        check (manager.saveAs ("Kick").wasOk(), "save as \"Kick\" (after confirming the overwrite)");
        const auto files = folder.findChildFiles (juce::File::findFiles, false, "*");
        check (files.size() == 1 && files[0].getFileName() == "Kick.heftpreset",
               "a single file, now spelled Kick.heftpreset: " + (files.isEmpty() ? juce::String ("none") : files[0].getFileName()));
        check (userNames (manager) == juce::StringArray { "Kick" }, "listed once, as Kick");
        check (manager.getCurrent().source == PresetManager::Source::user && manager.getCurrentName() == "Kick",
               "the current preset is Kick, like the list");

        PresetFile::Values onDisk {};
        PresetFile::read (folder.getChildFile ("Kick.heftpreset"), PresetFile::makeSpecs (apvts), onDisk);
        check (onDisk[0] == 60.0f, "with the new values");
        resetParams (apvts);
    }
    endSection();
}

//==============================================================================
void testInterruptedRenames (HeftAudioProcessor& processor, const juce::File& scratch)
{
    section ("Leftovers of an interrupted rename are recovered, never deleted");
    {
        const auto folder = scratch.getChildFile ("Renames");
        PresetManager manager (processor.apvts, folder);
        const auto specs = PresetFile::makeSpecs (processor.apvts);
        manager.saveAs ("Kick");

        // A rename cut short leaves the preset itself under an intermediate name.
        const auto now = juce::Time::getCurrentTime();
        const auto oldSnare = folder.getChildFile (".Snare.0123456789abcdef.heftpreset-renaming");
        const auto oldKick  = folder.getChildFile (".Kick.fedcba9876543210.heftpreset-renaming");
        const auto recent   = folder.getChildFile (".Bass.00112233aabbccdd.heftpreset-renaming");
        const auto notes    = folder.getChildFile ("notes.txt");
        const auto backup   = folder.getChildFile ("Kick.heftpreset-renaming.bak");

        PresetFile::write (folder.getChildFile ("source.heftpreset"), { 77.0f, 33.0f, 66.0f, -4.0f, 1.0f }, specs, "0.4.1");
        folder.getChildFile ("source.heftpreset").moveFileTo (oldSnare);

        for (const auto& file : { oldKick, recent, notes, backup })
            file.replaceWithText ("<HeftPreset formatVersion=\"1\"/>");

        for (const auto& file : { oldSnare, oldKick, notes, backup })
            file.setLastModificationTime (now - juce::RelativeTime::minutes (11));

        manager.refreshList();
        check (! oldSnare.exists() && userNames (manager).contains ("Snare"), "an old leftover gets its name back and is listed");

        PresetFile::Values values {};
        check (PresetFile::read (folder.getChildFile ("Snare.heftpreset"), specs, values).wasOk() && values[0] == 77.0f
                   && values[4] == 1.0f,
               "with its content intact");
        check (! oldKick.exists() && userNames (manager).contains ("Kick (recovered)") && userNames (manager).contains ("Kick"),
               "if the name is taken it becomes \"Kick (recovered)\"");
        check (recent.exists() && ! userNames (manager).contains ("Bass"), "a recent one is left alone (a rename may be in progress)");
        check (notes.exists() && backup.exists(), "nothing else is touched");
        check (folder.findChildFiles (juce::File::findFiles, false, "*").size() == 6, "and nothing was deleted");
    }
    endSection();
}

//==============================================================================
void testRenameCase (HeftAudioProcessor& processor, const juce::File& scratch)
{
    section ("Rename that only changes the case");
    {
        auto& apvts = processor.apvts;
        const auto folder = scratch.getChildFile ("Rename case");
        PresetManager manager (apvts, folder);

        setParam (apvts, ParamIDs::fatness, 45.0f);
        manager.saveAs ("kick");
        setParam (apvts, ParamIDs::color, 70.0f);
        check (manager.getCurrentName() == "kick" && manager.isModified(), "\"kick *\" is loaded");

        check (manager.renameCurrent ("Kick").wasOk(), "rename to \"Kick\"");
        const auto files = folder.findChildFiles (juce::File::findFiles, false, "*");
        check (files.size() == 1 && files[0].getFileName() == "Kick.heftpreset",
               "a single file, now spelled Kick.heftpreset: " + (files.isEmpty() ? juce::String ("none") : files[0].getFileName()));
        check (userNames (manager) == juce::StringArray { "Kick" }, "listed once, as Kick");
        check (manager.getCurrentName() == "Kick" && manager.isModified(), "the current preset is Kick, and the asterisk didn't change");

        manager.saveAs ("Snare");
        manager.load (*findEntry (manager, PresetManager::Source::user, "Kick"));
        check (manager.renameCurrent ("SNARE").failed() && userNames (manager) == juce::StringArray { "Kick", "Snare" },
               "renaming onto another preset (in any case) still never overwrites it");
        resetParams (apvts);
    }
    endSection();
}

//==============================================================================
void testNoAllocations()
{
    section ("Audio thread: processing never allocates memory");
    {
        HeftAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> announced (2, 512), bigger (2, 2048);   // bigger: processed in chunks
        juce::MidiBuffer midi;
        juce::Random random (1);
        int normal = 0, bypassed = 0, chunked = 0;

        const auto run = [&] (juce::AudioBuffer<float>& buffer, bool bypass, int& count)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, random.nextFloat() - 0.5f);

            allocationCount = 0;
            countingAllocations = true;

            if (bypass)
                processor.processBlockBypassed (buffer, midi);
            else
                processor.processBlock (buffer, midi);

            countingAllocations = false;
            count += allocationCount;
        };

        // Every knob and switch moves between blocks, so the coefficients, the clipper
        // and both crossfades all run.
        for (int round = 0; round < 4; ++round)
        {
            setParam (processor.apvts, ParamIDs::fatness, 10.0f + 25.0f * (float) round);
            setParam (processor.apvts, ParamIDs::color, 90.0f - 25.0f * (float) round);
            setParam (processor.apvts, ParamIDs::mix, 40.0f + 20.0f * (float) round);
            setParam (processor.apvts, ParamIDs::output, -6.0f + 3.0f * (float) round);
            setParam (processor.apvts, ParamIDs::clipper, (float) (round % 2));
            setParam (processor.apvts, ParamIDs::levelMatch, (float) (round / 2));

            run (announced, false, normal);
            run (announced, true, bypassed);
            run (bigger, false, chunked);
        }

        check (normal == 0, "processBlock (" + juce::String (normal) + " allocations)");
        check (bypassed == 0, "processBlockBypassed (" + juce::String (bypassed) + " allocations)");
        check (chunked == 0, "a block bigger than announced (" + juce::String (chunked) + " allocations)");

        allocationCount = 0;
        countingAllocations = true;
        const juce::String text (random.nextInt (1000) + 100000);
        countingAllocations = false;
        check (allocationCount > 0 && text.isNotEmpty(), "the counter does see allocations");
    }
    endSection();
}

void testOversampler()
{
    section ("Oversampler: bit-identical to juce::dsp::Oversampling");
    {
        Oversampler ours (2, 2);
        juce::dsp::Oversampling<float> reference (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        ours.initProcessing (512);
        reference.initProcessing (512);
        check (ours.getLatencyInSamples() == reference.getLatencyInSamples(),
               "same latency (" + juce::String (ours.getLatencyInSamples(), 3) + " samples)");

        juce::AudioBuffer<float> a (2, 512), b (2, 512);
        juce::Random random (9);
        bool upSame = true, downSame = true;

        for (int round = 0; round < 20; ++round)
            for (const int numSamples : { 512, 1, 17, 300, 64 })
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const auto x = 2.0f * random.nextFloat() - 1.0f;
                        a.setSample (ch, i, x);
                        b.setSample (ch, i, x);
                    }

                auto blockA = juce::dsp::AudioBlock<float> (a).getSubBlock (0, (size_t) numSamples);
                auto blockB = juce::dsp::AudioBlock<float> (b).getSubBlock (0, (size_t) numSamples);
                auto upA = ours.processSamplesUp (blockA);
                auto upB = reference.processSamplesUp (blockB);

                for (size_t ch = 0; ch < 2; ++ch)
                {
                    upSame = upSame && std::memcmp (upA.getChannelPointer (ch), upB.getChannelPointer (ch),
                                                    upA.getNumSamples() * sizeof (float)) == 0;

                    // Something nonlinear in between, as in the plugin.
                    for (size_t i = 0; i < upA.getNumSamples(); ++i)
                        upA.getChannelPointer (ch)[i] = upB.getChannelPointer (ch)[i] = std::tanh (3.0f * upB.getChannelPointer (ch)[i]);
                }

                ours.processSamplesDown (blockA);
                reference.processSamplesDown (blockB);

                for (int ch = 0; ch < 2; ++ch)
                    downSame = downSame && std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), (size_t) numSamples * sizeof (float)) == 0;
            }

        check (upSame, "4x up: every sample identical, with blocks of 512, 1, 17, 300 and 64");
        check (downSame, "4x down (with the fractional delay): every sample identical");
    }
    endSection();
}

void testTailLength()
{
    section ("Host: the tail covers the latency and the DC blocker's decay");
    {
        HeftAudioProcessor processor;
        check (processor.getTailLengthSeconds() > 0.0 && std::isfinite (processor.getTailLengthSeconds()),
               "finite and above 0 before the host sets a sample rate");

        processor.setRateAndBufferSizeDetails (48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        const auto tail = processor.getTailLengthSeconds();
        check (tail > processor.getLatencySamples() / 48000.0 + 0.2 && tail < 1.0,
               "a bit over the latency + 0.2 s, under 1 s (" + juce::String (tail, 3) + " s)");
    }
    endSection();
}

void testBrokenInput()
{
    section ("Audio thread: NaN, infinity or absurd levels in the input don't stick");
    {
        // 12 s of noise with Level Match on; "bad" (if not 0) replaces one sample at 1 s.
        const auto render = [] (float bad)
        {
            HeftAudioProcessor processor;
            setParam (processor.apvts, ParamIDs::fatness, 60.0f);
            setParam (processor.apvts, ParamIDs::mix, 70.0f);
            setParam (processor.apvts, ParamIDs::clipper, 1.0f);
            setParam (processor.apvts, ParamIDs::levelMatch, 1.0f);
            processor.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> buffer (2, 48000 * 12);
            juce::Random random (5);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, 0.5f * random.nextFloat() - 0.25f);

            if (bad != 0.0f)
                buffer.setSample (0, 48000, bad);

            juce::MidiBuffer midi;

            for (int start = 0; start < buffer.getNumSamples(); start += 512)
            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start, 512);
                processor.processBlock (view, midi);
            }

            return buffer;
        };

        const auto clean = render (0.0f);
        const std::pair<const char*, float> cases[] { { "NaN", std::numeric_limits<float>::quiet_NaN() },
                                                      { "+infinity", std::numeric_limits<float>::infinity() },
                                                      { "-infinity", -std::numeric_limits<float>::infinity() },
                                                      { "1e30", 1.0e30f } };

        for (const auto& [name, bad] : cases)
        {
            const auto out = render (bad);
            bool finite = true;
            float maxDiff = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = out.getNumSamples() - 48000; i < out.getNumSamples(); ++i)
                {
                    finite = finite && std::isfinite (out.getSample (ch, i));
                    maxDiff = std::max (maxDiff, std::abs (out.getSample (ch, i) - clean.getSample (ch, i)));
                }

            check (finite && maxDiff < 1.0e-4f,   // -80 dBFS
                   juce::String ("one ") + name + " sample: 11 s later the output is the clean one again"
                       + (finite ? " (" + juce::String (juce::Decibels::gainToDecibels (maxDiff, -200.0f), 1) + " dBFS)" : " (NaN)"));
        }
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
        testFolderAvailability (processor, scratch.folder);
        testTemporaryFiles (processor, scratch.folder);
        testSaveAsCase (processor, scratch.folder);
        testInterruptedRenames (processor, scratch.folder);
        testRenameCase (processor, scratch.folder);
    }

    testProjectState();
    testABComparator (scratch.folder);
    testFactoryPresets (scratch.folder);
    testNoAllocations();
    testBrokenInput();
    testTailLength();
    testOversampler();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
