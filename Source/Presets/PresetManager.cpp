#include "PresetManager.h"

#include <algorithm>

#if HEFT_HAS_FACTORY_PRESETS
 #include "HeftFactoryData.h"
#endif

namespace
{
    using Source = PresetManager::Source;

    juce::String sourceToString (Source source)
    {
        switch (source)
        {
            case Source::defaultPreset: return "default";
            case Source::factory:       return "factory";
            case Source::user:          return "user";
            case Source::none:          break;
        }

        return "none";
    }

    Source sourceFromString (const juce::String& text)
    {
        if (text == "default") return Source::defaultPreset;
        if (text == "factory") return Source::factory;
        if (text == "user")    return Source::user;
        return Source::none;
    }

    // Values are stored as properties named after the parameter IDs, in real units.
    void writeValues (juce::ValueTree& tree, const PresetFile::Values& values, const PresetFile::Specs& specs)
    {
        for (size_t i = 0; i < PresetFile::numParams; ++i)
            tree.setProperty (juce::Identifier (specs[i].id), values[i], nullptr);
    }

    // Missing or odd values fall back to `fallback` or are clamped; never an error.
    PresetFile::Values readValues (const juce::ValueTree& tree, const PresetFile::Specs& specs,
                                   const PresetFile::Values& fallback)
    {
        auto values = fallback;

        for (size_t i = 0; i < PresetFile::numParams; ++i)
        {
            // Text that isn't a number would read as 0; keep the fallback instead.
            const auto text = tree[juce::Identifier (specs[i].id)].toString().trim();

            if (text.isNotEmpty() && text.containsOnly ("0123456789.-+eE"))
                values[i] = PresetFile::sanitise (specs[i], (float) text.getDoubleValue());
        }

        return values;
    }

    const juce::Identifier slotTypes[] { "SlotA", "SlotB" };
    const juce::Identifier snapshotType { "Snapshot" }, activeId { "active" },
                           presetSourceId { "presetSource" }, presetNameId { "presetName" };
}

bool PresetManager::Ref::refersTo (const Ref& other) const
{
    if (source != other.source)
        return false;

    if (source == Source::none || source == Source::defaultPreset)
        return true;

    // User presets are files, and Windows file names ignore case.
    return source == Source::user ? name.equalsIgnoreCase (other.name) : name == other.name;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, juce::File folder)
    : apvts (state),
      specs (PresetFile::makeSpecs (state)),
      userFolder (std::move (folder))
{
    loadFactoryPresets();
    userFolderAvailable = checkUserFolderAvailable();

    // A new instance starts on "Default", with both A/B slots the same.
    const auto defaults = PresetFile::defaults (specs);

    for (auto& slot : slots)
        slot = { defaults, { Source::defaultPreset, defaultName }, defaults };
}

PresetManager::~PresetManager() = default;

juce::File PresetManager::defaultUserFolder()
{
    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return documents == juce::File() ? juce::File() : documents.getChildFile ("Heft").getChildFile ("Presets");
}

bool PresetManager::checkUserFolderAvailable() const
{
    // The folder itself is created on demand; what must exist is Documents (its grandparent).
    return userFolder != juce::File()
        && (userFolder.isDirectory() || userFolder.getParentDirectory().getParentDirectory().isDirectory());
}

void PresetManager::loadFactoryPresets()
{
   #if HEFT_HAS_FACTORY_PRESETS
    for (int i = 0; i < HeftFactoryData::namedResourceListSize; ++i)
    {
        const auto* resource = HeftFactoryData::namedResourceList[i];
        const juce::String fileName (HeftFactoryData::getNamedResourceOriginalFilename (resource));

        if (! fileName.endsWithIgnoreCase (PresetFile::extension))
            continue;

        int size = 0;
        const auto* data = HeftFactoryData::getNamedResource (resource, size);

        FactoryPreset preset { fileName, PresetFile::factoryDisplayName (fileName), {} };
        const auto result = PresetFile::fromXml (juce::String::fromUTF8 (data, size), specs, preset.values);

        // A factory preset that doesn't parse: fix the file in Presets/Factory before releasing.
        jassert (result.wasOk());

        if (result.wasOk())
            factoryPresets.push_back (preset);
    }

    // The file names set the order ("01 Kick", "02 Bass"...); the numbers aren't shown.
    std::sort (factoryPresets.begin(), factoryPresets.end(), [] (const auto& a, const auto& b)
               { return a.fileName.compareNatural (b.fileName) < 0; });
   #endif
}

//==============================================================================
void PresetManager::refreshList()
{
    userFolderAvailable = checkUserFolderAvailable();
    entries.clear();
    entries.push_back ({ { Source::defaultPreset, defaultName }, {} });

    for (const auto& preset : factoryPresets)
        entries.push_back ({ { Source::factory, preset.name }, {} });

    removeStaleTemporaryFiles();

    {
        // Natural order: "Kick 2" before "Kick 10".
        auto files = userPresetFiles();

        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });

        for (const auto& file : files)
            entries.push_back ({ { Source::user, file.getFileNameWithoutExtension() }, file });
    }
}

//==============================================================================
PresetManager::Ref PresetManager::getCurrent() const
{
    const juce::ScopedLock sl (lock);
    return slots[(size_t) activeSlot].preset;
}

juce::String PresetManager::getCurrentName() const
{
    const auto current = getCurrent();
    return current.source == Source::none ? untitledName : current.name;
}

bool PresetManager::isModified() const
{
    const juce::ScopedLock sl (lock);
    const auto& slot = slots[(size_t) activeSlot];

    // "Untitled" has nothing to compare against, so it never shows an asterisk.
    return slot.preset.source != Source::none && ! PresetFile::matches (currentValues(), slot.snapshot, specs);
}

PresetManager::Values PresetManager::currentValues() const
{
    Values values {};

    for (size_t i = 0; i < PresetFile::numParams; ++i)
    {
        auto* param = apvts.getParameter (presetParamIds[i]);
        values[i] = PresetFile::sanitise (specs[i], param->convertFrom0to1 (param->getValue()));
    }

    return values;
}

// Through the host, one gesture per parameter, so the DAW knows (and records
// automation if it's writing it). Never written straight into apvts.state.
void PresetManager::applyValues (const Values& values)
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (size_t i = 0; i < PresetFile::numParams; ++i)
    {
        auto* param = apvts.getParameter (presetParamIds[i]);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (values[i]));
        param->endChangeGesture();
    }
}

void PresetManager::setCurrent (const Ref& ref, const Values& snapshot)
{
    {
        const juce::ScopedLock sl (lock);
        auto& slot = slots[(size_t) activeSlot];
        slot.preset   = ref;
        slot.snapshot = snapshot;
    }

    changed();
}

void PresetManager::changed()
{
    ++changeCount;
    sendChangeMessage();   // thread-safe: the listeners are called on the message thread
}

juce::Result PresetManager::load (const Entry& entry)
{
    Values values {};

    switch (entry.ref.source)
    {
        case Source::defaultPreset:
            values = PresetFile::defaults (specs);
            break;

        case Source::factory:
        {
            const auto it = std::find_if (factoryPresets.begin(), factoryPresets.end(),
                                          [&] (const auto& p) { return p.name == entry.ref.name; });

            if (it == factoryPresets.end())
                return juce::Result::fail ("The factory preset \"" + entry.ref.name + "\" doesn't exist in this version of Heft.");

            values = it->values;
            break;
        }

        case Source::user:
        {
            const auto file = entry.file != juce::File() ? entry.file : userFileFor (entry.ref.name);
            const auto result = PresetFile::read (file, specs, values);

            if (result.failed())
            {
                refreshList();
                changed();
                return juce::Result::fail ("\"" + entry.ref.name + "\" " + result.getErrorMessage());
            }

            break;
        }

        case Source::none:
            return juce::Result::fail ("There is no preset to load.");
    }

    applyValues (values);
    setCurrent (entry.ref, values);
    return juce::Result::ok();
}

// Arrows: through Default, factory and user presets as listed, wrapping at both ends.
// From "Untitled" (or a preset that's gone), next goes to the first and previous to the last.
juce::Result PresetManager::step (int delta)
{
    refreshList();

    const auto current = getCurrent();
    const auto count = (int) entries.size();
    auto index = -1;

    for (int i = 0; i < count; ++i)
        if (entries[(size_t) i].ref.refersTo (current))
            index = i;

    const auto target = index < 0 ? (delta > 0 ? 0 : count - 1)
                                  : (index + delta + count) % count;

    return load (entries[(size_t) target]);
}

//==============================================================================
juce::Result PresetManager::ensureUserFolder()
{
    userFolderAvailable = checkUserFolderAvailable();

    if (! userFolderAvailable)
        return juce::Result::fail ("Your Documents folder can't be found, so user presets are unavailable.");

    if (userFolder.isDirectory())
        return juce::Result::ok();

    const auto result = userFolder.createDirectory();

    return result.wasOk() ? result
                          : juce::Result::fail ("Couldn't create the presets folder \"" + userFolder.getFullPathName()
                                                + "\": " + result.getErrorMessage());
}

juce::File PresetManager::userFileFor (const juce::String& name) const
{
    for (const auto& file : userPresetFiles())
        if (file.getFileNameWithoutExtension().equalsIgnoreCase (name))
            return file;

    return userFolder.getChildFile (name + PresetFile::extension);
}

// Only files directly in the folder whose extension is exactly .heftpreset (any case):
// "Kick.heftpreset" yes, ".Kick.1a2b.heftpreset-tmp" no.
juce::Array<juce::File> PresetManager::userPresetFiles() const
{
    juce::Array<juce::File> presets;

    if (userFolder.isDirectory())
        for (const auto& file : userFolder.findChildFiles (juce::File::findFiles, false, "*"))
            if (file.getFileExtension().equalsIgnoreCase (PresetFile::extension)
                && file.getFileNameWithoutExtension().isNotEmpty())
                presets.add (file);

    return presets;
}

// Leftovers of saves that were cut short. Only that exact extension, and only when
// older than 10 minutes, so a save that another Heft instance is doing right now is
// never touched.
void PresetManager::removeStaleTemporaryFiles() const
{
    if (! userFolder.isDirectory())
        return;

    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::minutes (10);

    for (const auto& file : userFolder.findChildFiles (juce::File::findFiles, false, "*"))
        if (file.getFileExtension().equalsIgnoreCase (PresetFile::tempExtension) && file.getLastModificationTime() < cutoff)
            file.deleteFile();
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    return userFileFor (name).existsAsFile();
}

bool PresetManager::canSaveInPlace() const
{
    return getCurrent().source == Source::user;
}

juce::Result PresetManager::save()
{
    const auto current = getCurrent();

    if (current.source != Source::user)
        return juce::Result::fail ("Only user presets can be saved in place.");

    if (const auto folder = ensureUserFolder(); folder.failed())
        return folder;

    // If the file was deleted or renamed outside Heft, this writes it again.
    const auto values = currentValues();
    const auto result = PresetFile::write (userFileFor (current.name), values, specs, JucePlugin_VersionString);

    if (result.failed())
        return result;

    setCurrent (current, values);
    refreshList();
    return juce::Result::ok();
}

juce::Result PresetManager::saveAs (const juce::String& name)
{
    if (const auto error = PresetFile::validateName (name); error.isNotEmpty())
        return juce::Result::fail (error);

    if (const auto folder = ensureUserFolder(); folder.failed())
        return folder;

    // userFileFor() finds an existing preset with this name in any case ("kick" for "Kick"),
    // so overwriting writes into that file instead of creating a second spelling.
    const auto target = userFileFor (name);
    const auto values = currentValues();
    const auto result = PresetFile::write (target, values, specs, JucePlugin_VersionString);

    if (result.failed())
        return result;

    // Windows keeps the old spelling of an overwritten file, so rename it to exactly what
    // was typed. If that fails the save still worked: keep the old name everywhere so the
    // bar and the list agree, without an error.
    auto savedName = name;

    if (spellingOnDisk (target) != name + PresetFile::extension && ! renameExactly (target, name))
        savedName = spellingOnDisk (target).upToLastOccurrenceOf (".", false, false);

    setCurrent ({ Source::user, savedName }, values);
    refreshList();
    return juce::Result::ok();
}

juce::String PresetManager::spellingOnDisk (const juce::File& file) const
{
    for (const auto& child : userFolder.findChildFiles (juce::File::findFiles, false, "*"))
        if (child.getFileName().equalsIgnoreCase (file.getFileName()))
            return child.getFileName();

    return {};
}

// Renames a user preset file to exactly newName, also when only the case changes ("kick"
// to "Kick"), which Windows treats as the same file. If a direct move doesn't change the
// spelling, it goes through an intermediate name, and back to the original if that fails.
bool PresetManager::renameExactly (const juce::File& from, const juce::String& newName) const
{
    const auto target = userFolder.getChildFile (newName + PresetFile::extension);

    if (from.moveFileTo (target) && spellingOnDisk (target) == target.getFileName())
        return true;

    const auto original = userFolder.getChildFile (spellingOnDisk (target));
    const auto between = userFolder.getChildFile ("." + newName + "."
                                                  + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
                                                  + ".heftpreset-renaming");

    if (! original.moveFileTo (between))
        return false;

    if (between.moveFileTo (target) && spellingOnDisk (target) == target.getFileName())
        return true;

    between.moveFileTo (original);
    return false;
}

juce::Result PresetManager::renameCurrent (const juce::String& newName)
{
    const auto current = getCurrent();

    if (current.source != Source::user)
        return juce::Result::fail ("Only user presets can be renamed.");

    if (const auto error = PresetFile::validateName (newName); error.isNotEmpty())
        return juce::Result::fail (error);

    if (newName == current.name)
        return juce::Result::ok();

    const auto from = userFileFor (current.name);

    if (! from.existsAsFile())
    {
        refreshList();
        return juce::Result::fail ("\"" + current.name + "\" no longer exists.");
    }

    // Renaming never overwrites, but changing only the case ("kick" to "Kick") is fine.
    if (! newName.equalsIgnoreCase (current.name) && userPresetExists (newName))
        return juce::Result::fail ("A preset called \"" + newName + "\" already exists.");

    if (! from.moveFileTo (userFolder.getChildFile (newName + PresetFile::extension)))
        return juce::Result::fail ("Couldn't rename \"" + current.name + "\". The file may be read-only or in use.");

    {
        const juce::ScopedLock sl (lock);

        for (auto& slot : slots)
            if (slot.preset.refersTo (current))
                slot.preset.name = newName;   // the asterisk doesn't change
    }

    refreshList();
    changed();
    return juce::Result::ok();
}

juce::Result PresetManager::deleteCurrent()
{
    const auto current = getCurrent();

    if (current.source != Source::user)
        return juce::Result::fail ("Only user presets can be deleted.");

    const auto file = userFileFor (current.name);

    if (file.existsAsFile() && ! removeFile (file))
        return juce::Result::fail ("Couldn't delete \"" + current.name + "\". The file may be in use.");

    // The sound doesn't change; whatever pointed at the deleted preset becomes "Untitled".
    {
        const juce::ScopedLock sl (lock);

        for (auto& slot : slots)
            if (slot.preset.refersTo (current))
                slot.preset = {};
    }

    refreshList();
    changed();
    return juce::Result::ok();
}

//==============================================================================
juce::String PresetManager::ImportResult::summary() const
{
    const auto importedText = juce::String (imported) + " imported";

    if (problems.isEmpty())
        return importedText + ".";

    return importedText + ", " + juce::String (problems.size()) + " skipped: " + problems.joinIntoString (" ");
}

// Valid files are copied into the user folder under their own name, with " (2)",
// " (3)"... if it's taken: never a silent overwrite. Then the first one is loaded.
PresetManager::ImportResult PresetManager::importFiles (const juce::Array<juce::File>& files)
{
    ImportResult result;

    if (const auto folder = ensureUserFolder(); folder.failed())
    {
        result.problems.add (folder.getErrorMessage());
        return result;
    }

    std::optional<Entry> firstImported;

    for (const auto& file : files)
    {
        Values ignored {};

        if (const auto valid = PresetFile::read (file, specs, ignored); valid.failed())
        {
            result.problems.add (file.getFileName() + " " + valid.getErrorMessage());
            continue;
        }

        const auto baseName = file.getFileNameWithoutExtension();
        auto name = baseName;

        for (int n = 2; userPresetExists (name); ++n)
            name = baseName + " (" + juce::String (n) + ")";

        const auto target = userFolder.getChildFile (name + PresetFile::extension);

        if (! file.copyFileTo (target))
        {
            result.problems.add ("Couldn't copy " + file.getFileName() + ".");
            continue;
        }

        ++result.imported;

        if (! firstImported)
            firstImported = Entry { { Source::user, name }, target };
    }

    refreshList();

    if (firstImported)
        if (const auto loaded = load (*firstImported); loaded.failed())
            result.problems.add (loaded.getErrorMessage());

    changed();
    return result;
}

// The chooser belongs to the manager (which lives as long as the processor), so
// closing the editor while it's open is safe. onDone is called on the message thread.
void PresetManager::chooseFilesToImport (std::function<void (ImportResult)> onDone)
{
    chooser = std::make_unique<juce::FileChooser> ("Import Heft presets", juce::File(), "*" + PresetFile::extension);

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectMultipleItems;

    chooser->launchAsync (flags, [this, onDone = std::move (onDone)] (const juce::FileChooser& fc)
    {
        const auto files = fc.getResults();

        if (files.isEmpty())
            return;

        const auto result = importFiles (files);

        if (onDone)
            onDone (result);
    });
}

juce::Result PresetManager::revealUserFolder()
{
    if (const auto folder = ensureUserFolder(); folder.failed())
        return folder;

    if (! userFolder.startAsProcess())
        return juce::Result::fail ("Couldn't open \"" + userFolder.getFullPathName() + "\".");

    return juce::Result::ok();
}

//==============================================================================
PresetManager::Slot PresetManager::getActiveSlot() const
{
    const juce::ScopedLock sl (lock);
    return activeSlot == 0 ? Slot::a : Slot::b;
}

void PresetManager::switchTo (Slot slot)
{
    const auto target = slot == Slot::a ? 0 : 1;
    Values targetValues {};

    {
        const juce::ScopedLock sl (lock);

        if (target == activeSlot)
            return;

        slots[(size_t) activeSlot].values = currentValues();
        targetValues = slots[(size_t) target].values;
        activeSlot = target;
    }

    // Outside the lock: hosts may ask for the state while parameters change. The
    // smoothing in the DSP makes the change a short glide, never a click.
    applyValues (targetValues);
    changed();
}

void PresetManager::copyActiveToOther()
{
    {
        const juce::ScopedLock sl (lock);
        auto& active = slots[(size_t) activeSlot];
        active.values = currentValues();
        slots[(size_t) (1 - activeSlot)] = active;
    }

    changed();
}

//==============================================================================
void PresetManager::writeState (juce::ValueTree& state)
{
    const juce::ScopedLock sl (lock);

    // The parameters are the truth for the active slot.
    slots[(size_t) activeSlot].values = currentValues();

    state.setProperty (stateVersionId, stateVersion, nullptr);
    state.removeChild (state.getChildWithName (abStateType), nullptr);

    juce::ValueTree ab (abStateType);
    ab.setProperty (activeId, activeSlot == 0 ? "A" : "B", nullptr);

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& slot = slots[i];
        juce::ValueTree tree (slotTypes[i]);
        writeValues (tree, slot.values, specs);
        tree.setProperty (presetSourceId, sourceToString (slot.preset.source), nullptr);
        tree.setProperty (presetNameId, slot.preset.name, nullptr);

        juce::ValueTree snapshot (snapshotType);
        writeValues (snapshot, slot.snapshot, specs);
        tree.appendChild (snapshot, nullptr);

        ab.appendChild (tree, nullptr);
    }

    state.appendChild (ab, nullptr);
}

// Called after the parameters were restored. Never changes them; missing or odd data
// gets reasonable values instead.
void PresetManager::readState (const juce::ValueTree& abState)
{
    const auto current  = currentValues();
    const auto defaults = PresetFile::defaults (specs);

    {
        const juce::ScopedLock sl (lock);

        if (! abState.isValid())
        {
            // A project from 0.3.x: both slots take the current settings, A is active, and
            // the preset is "Default" if the knobs are at their defaults, else "Untitled".
            const auto isDefault = PresetFile::matches (current, defaults, specs);
            const SlotData slot { current,
                                  isDefault ? Ref { Source::defaultPreset, defaultName } : Ref {},
                                  isDefault ? defaults : current };
            slots = { slot, slot };
            activeSlot = 0;
        }
        else
        {
            activeSlot = abState[activeId].toString() == "B" ? 1 : 0;

            for (size_t i = 0; i < slots.size(); ++i)
            {
                const auto tree = abState.getChildWithName (slotTypes[i]);
                auto& slot = slots[i];

                slot.values = (int) i == activeSlot ? current : readValues (tree, specs, defaults);
                slot.preset = { sourceFromString (tree[presetSourceId].toString()), tree[presetNameId].toString() };

                if (slot.preset.source == Source::defaultPreset)
                    slot.preset.name = defaultName;
                else if (slot.preset.source == Source::none || slot.preset.name.isEmpty())
                    slot.preset = {};

                const auto snapshot = tree.getChildWithName (snapshotType);
                slot.snapshot = snapshot.isValid() ? readValues (snapshot, specs, slot.values) : slot.values;
            }
        }
    }

    changed();
}
