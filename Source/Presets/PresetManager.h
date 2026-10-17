#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include "PresetFile.h"

// Presets and the A/B comparator. It lives in the processor, so presets and A/B work
// with the editor closed and are saved with the project. The editor only asks and shows.
//
// Threads: everything runs on the message thread except writeState() and readState(),
// which hosts may call from other threads; those only copy memory, under `lock`. The
// audio thread never touches this class: values reach the DSP through the parameters,
// changed with setValueNotifyingHost inside change gestures so the host records them.
class PresetManager final : public juce::ChangeBroadcaster
{
public:
    enum class Source { none, defaultPreset, factory, user };   // none = "Untitled"

    // A preset is identified by where it comes from plus its name, so a user preset
    // can share a name with a factory one.
    struct Ref
    {
        Source source = Source::none;
        juce::String name;

        bool refersTo (const Ref& other) const;
    };

    struct Entry
    {
        Ref ref;
        juce::File file;   // user presets only
    };

    using Values = PresetFile::Values;

    inline static const juce::String defaultName { "Default" };
    inline static const juce::String untitledName { "Untitled" };

    // userFolder: where user presets live. Tests pass a temporary folder.
    explicit PresetManager (juce::AudioProcessorValueTreeState&, juce::File userFolder = defaultUserFolder());
    ~PresetManager() override;

    static juce::File defaultUserFolder();   // Documents\Heft\Presets
    const juce::File& getUserFolder() const { return userFolder; }

    // False if Documents can't be found. Cached, without touching the disk: the UI asks
    // 30 times a second and Documents may be on a network drive or OneDrive. Updated in
    // the constructor, refreshList() and whenever the folder is about to be used.
    bool isUserFolderAvailable() const { return userFolderAvailable.load(); }

    //==============================================================================
    // The list: Default, factory presets, then user presets. refreshList() re-reads
    // the user folder, so files copied there by hand show up without a restart.
    void refreshList();
    const std::vector<Entry>& getEntries() const { return entries; }
    int getNumFactoryEntries() const { return 1 + (int) factoryPresets.size(); }   // including Default

    //==============================================================================
    // The current preset is the one of the active A/B slot.
    Ref getCurrent() const;
    juce::String getCurrentName() const;     // "Untitled" if there's none
    bool isModified() const;                 // values differ from the preset as loaded

    juce::Result load (const Entry&);
    juce::Result loadNext()     { return step (1); }
    juce::Result loadPrevious() { return step (-1); }

    bool canSaveInPlace() const;             // the current preset is a user preset
    juce::Result save();                     // overwrites the current user preset
    bool userPresetExists (const juce::String& name) const;   // case-insensitive, like Windows
    juce::Result saveAs (const juce::String& name);           // overwrites: ask the user first
    juce::Result renameCurrent (const juce::String& newName); // never overwrites
    juce::Result deleteCurrent();                              // to the Recycle Bin

    struct ImportResult
    {
        int imported = 0;
        juce::StringArray problems;   // "X is not a Heft preset."
        juce::String summary() const; // "2 imported, 1 skipped: X is not a Heft preset."
    };

    ImportResult importFiles (const juce::Array<juce::File>&);
    void chooseFilesToImport (std::function<void (ImportResult)> onDone);   // async file chooser
    juce::Result revealUserFolder();

    //==============================================================================
    // A/B: two complete settings; the knobs always show the active one.
    enum class Slot { a, b };

    Slot getActiveSlot() const;
    void switchTo (Slot);
    void copyActiveToOther();

    //==============================================================================
    // Project state (any thread, memory only). Adds stateVersion and an ABState child
    // to the tree the processor saves; reads them back, or migrates a 0.3.x state.
    void writeState (juce::ValueTree& state);
    void readState (const juce::ValueTree& abState);

    inline static const juce::Identifier abStateType { "ABState" };
    inline static const juce::Identifier stateVersionId { "stateVersion" };
    static constexpr int stateVersion = 2;

    // Goes up on every change, so the UI can refresh cheaply from its timer.
    juce::uint32 getChangeCount() const { return changeCount.load(); }

    // How a deleted preset goes away. Defaults to the Recycle Bin; tests delete for real.
    std::function<bool (const juce::File&)> removeFile = [] (const juce::File& f) { return f.moveToTrash(); };

private:
    struct SlotData
    {
        Values values {};     // only meaningful for the inactive slot; the active one lives in the parameters
        Ref preset;
        Values snapshot {};   // the preset's values as loaded or saved, for the asterisk
    };

    struct FactoryPreset
    {
        juce::String fileName, name;
        Values values {};
    };

    Values currentValues() const;
    void applyValues (const Values&);
    void setCurrent (const Ref&, const Values& snapshot);
    void changed();
    juce::Result step (int delta);
    juce::Result ensureUserFolder();
    bool checkUserFolderAvailable() const;   // the real check, on disk
    juce::Array<juce::File> userPresetFiles() const;
    void removeStaleTemporaryFiles() const;
    juce::String spellingOnDisk (const juce::File&) const;   // the file name as Windows stores it
    bool renameExactly (const juce::File& from, const juce::String& newName) const;
    juce::File userFileFor (const juce::String& name) const;   // existing file (any case) or a new path
    void loadFactoryPresets();

    juce::AudioProcessorValueTreeState& apvts;
    const PresetFile::Specs specs;
    const juce::File userFolder;

    std::vector<FactoryPreset> factoryPresets;
    std::vector<Entry> entries;   // message thread only

    mutable juce::CriticalSection lock;   // guards slots and activeSlot
    std::array<SlotData, 2> slots;
    int activeSlot = 0;

    std::atomic<juce::uint32> changeCount { 0 };
    std::atomic<bool> userFolderAvailable { false };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
