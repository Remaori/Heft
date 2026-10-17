#pragma once

#include "Palette.h"
#include "Presets/PresetManager.h"

// The bar in the header: ◀  name  ▶  Save  ⋯    A | B  Copy
// It only asks the PresetManager to do things and shows the results; all the logic
// lives there. Dialogs are asynchronous and are added to the editor, so they appear
// over the plugin and never behind the DAW. If the editor closes while one is open,
// the callbacks check a SafePointer and do nothing to the deleted UI.
class PresetBar final : public juce::Component, private juce::ChangeListener
{
public:
    // Size at 100 % scale; the editor multiplies it by its scale.
    static constexpr int preferredWidth = 470, preferredHeight = 28;

    explicit PresetBar (PresetManager& m) : manager (m)
    {
        previousButton.getProperties().set ("glyph", "previous");
        nextButton.getProperties().set ("glyph", "next");
        moreButton.getProperties().set ("glyph", "more");
        nameButton.getProperties().set ("presetName", juce::String());
        slotAButton.getProperties().set ("segment", "left");
        slotBButton.getProperties().set ("segment", "right");

        previousButton.setTooltip ("Previous preset");
        nextButton.setTooltip ("Next preset");
        nameButton.setTooltip ("Choose a preset");
        saveButton.setTooltip ("Save changes to this preset, or save it as a new one");
        moreButton.setTooltip ("More preset options");
        slotAButton.setTooltip ("Compare: setting A");
        slotBButton.setTooltip ("Compare: setting B");

        previousButton.onClick = [this] { report ("Can't load the preset", manager.loadPrevious()); };
        nextButton.onClick     = [this] { report ("Can't load the preset", manager.loadNext()); };
        nameButton.onClick     = [this] { showPresetMenu(); };
        saveButton.onClick     = [this] { save(); };
        moreButton.onClick     = [this] { showMoreMenu(); };

        // A/B: clicking the letter that's already active does nothing (the manager ignores it).
        slotAButton.onClick = [this] { manager.switchTo (PresetManager::Slot::a); };
        slotBButton.onClick = [this] { manager.switchTo (PresetManager::Slot::b); };
        copyButton.onClick  = [this] { manager.copyActiveToOther(); };

        for (auto* button : { &previousButton, &nameButton, &nextButton, &saveButton, &moreButton,
                              &slotAButton, &slotBButton, &copyButton })
            addAndMakeVisible (button);

        manager.addChangeListener (this);
        refresh();
    }

    ~PresetBar() override
    {
        manager.removeChangeListener (this);

        if (dialog != nullptr)
            dialog->setLookAndFeel (nullptr);
    }

    // Called from the editor's timer too: the asterisk follows the knobs (and automation)
    // without parameter listeners.
    void refresh()
    {
        const auto name = manager.getCurrentName();
        const auto modified = manager.isModified();
        auto& props = nameButton.getProperties();

        if (props["presetName"].toString() != name || (bool) props["modified"] != modified)
        {
            props.set ("presetName", name);
            props.set ("modified", modified);
            nameButton.repaint();
        }

        saveButton.setEnabled (manager.canSaveInPlace() || manager.isUserFolderAvailable());

        const auto onA = manager.getActiveSlot() == PresetManager::Slot::a;

        if (slotAButton.getToggleState() != onA || slotBButton.getToggleState() == onA)
        {
            slotAButton.setToggleState (onA, juce::dontSendNotification);
            slotBButton.setToggleState (! onA, juce::dontSendNotification);
            copyButton.setTooltip (onA ? "Copy A to B" : "Copy B to A");
        }
    }

    void resized() override
    {
        const auto scale = (float) getHeight() / (float) preferredHeight;
        const auto gap = juce::roundToInt (1.5f * scale);   // the small space between neighbouring pills
        auto area = getLocalBounds();
        const auto take = [&] (float width) { return area.removeFromLeft (juce::roundToInt (width * scale)); };

        previousButton.setBounds (take (24.0f).withTrimmedRight (gap));
        nameButton.setBounds (take (210.0f).reduced (gap, 0));
        nextButton.setBounds (take (24.0f).withTrimmedLeft (gap));
        take (8.0f);
        saveButton.setBounds (take (52.0f).withTrimmedRight (gap));
        moreButton.setBounds (take (28.0f).withTrimmedLeft (gap));
        take (20.0f);
        slotAButton.setBounds (take (28.0f));   // the two halves touch: one A | B pill
        slotBButton.setBounds (take (28.0f));
        copyButton.setBounds (take (48.0f).withTrimmedLeft (gap * 2));
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    // Dialogs go into the editor (the top-level component of the plugin window).
    juce::Component& dialogParent() { return *getTopLevelComponent(); }

    static juce::String quoted (const juce::String& name)
    {
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x98")) + name + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x99"));
    }

    //==============================================================================
    void showPresetMenu()
    {
        manager.refreshList();   // picks up files copied into the folder by hand

        const auto entries = manager.getEntries();   // a copy: the list may change before the menu closes
        const auto numFactory = manager.getNumFactoryEntries();
        const auto current = manager.getCurrent();

        juce::PopupMenu menu;
        menu.addSectionHeader ("Factory");

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            if (i == numFactory)
                menu.addSectionHeader ("User");

            menu.addItem (i + 1, entries[(size_t) i].ref.name, true, entries[(size_t) i].ref.refersTo (current));
        }

        if ((int) entries.size() == numFactory)
        {
            menu.addSectionHeader ("User");
            menu.addItem (-1, manager.isUserFolderAvailable() ? "No user presets yet"
                                                              : "Documents folder not found: user presets are unavailable",
                          false);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton)
                                                      .withMinimumWidth (nameButton.getWidth()),
                            [safe = juce::Component::SafePointer<PresetBar> (this), entries] (int result)
                            {
                                if (safe != nullptr && result > 0 && result <= (int) entries.size())
                                    safe->report ("Can't load the preset", safe->manager.load (entries[(size_t) result - 1]));
                            });
    }

    void showMoreMenu()
    {
        const auto isUser = manager.canSaveInPlace();
        const auto folderOk = manager.isUserFolderAvailable();
        const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));

        juce::PopupMenu menu;
        menu.addItem (1, "Save as" + ellipsis, folderOk);
        menu.addItem (2, "Rename" + ellipsis, isUser);
        menu.addItem (3, "Delete" + ellipsis, isUser);
        menu.addSeparator();
        menu.addItem (4, "Import" + ellipsis, folderOk);
        menu.addItem (5, "Open presets folder", folderOk);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton),
                            [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
                            {
                                if (safe == nullptr)
                                    return;

                                switch (result)
                                {
                                    case 1: safe->saveAs(); break;
                                    case 2: safe->rename(); break;
                                    case 3: safe->deletePreset(); break;
                                    case 4: safe->import(); break;
                                    case 5: safe->report ("Can't open the presets folder", safe->manager.revealUserFolder()); break;
                                    default: break;
                                }
                            });
    }

    //==============================================================================
    // Save overwrites a user preset without asking; anything else becomes "Save as".
    void save()
    {
        if (manager.canSaveInPlace())
            report ("Can't save the preset", manager.save());
        else
            saveAs();
    }

    void saveAs()
    {
        const auto current = manager.getCurrent();
        const auto suggestion = current.source == PresetManager::Source::none ? juce::String() : current.name;

        askForName ("Save preset", suggestion, "Save", [this] (const juce::String& name)
        {
            if (const auto error = PresetFile::validateName (name); error.isNotEmpty())
            {
                showMessage ("Can't save the preset", error);
                return;
            }

            const auto doSave = [this, name] { report ("Can't save the preset", manager.saveAs (name)); };

            if (manager.userPresetExists (name))
                confirm ("Overwrite preset", "Overwrite " + quoted (name) + "?", "Overwrite", doSave);
            else
                doSave();
        });
    }

    void rename()
    {
        askForName ("Rename preset", manager.getCurrentName(), "Rename", [this] (const juce::String& name)
        {
            report ("Can't rename the preset", manager.renameCurrent (name));
        });
    }

    void deletePreset()
    {
        confirm ("Delete preset", "Delete preset " + quoted (manager.getCurrentName()) + "?", "Delete", [this]
        {
            report ("Can't delete the preset", manager.deleteCurrent());
        });
    }

    void import()
    {
        // The file chooser belongs to the manager; the result is shown only if we still exist.
        manager.chooseFilesToImport ([safe = juce::Component::SafePointer<PresetBar> (this)] (PresetManager::ImportResult result)
        {
            if (safe != nullptr && ! result.problems.isEmpty())
                safe->showMessage ("Import presets", result.summary());
        });
    }

public:
    // Dropping .heftpreset files on the window works like Import (some DAWs keep the drop).
    void importFiles (const juce::Array<juce::File>& files)
    {
        const auto result = manager.importFiles (files);

        if (! result.problems.isEmpty())
            showMessage ("Import presets", result.summary());
    }

private:
    //==============================================================================
    void report (const juce::String& title, const juce::Result& result)
    {
        if (result.failed())
            showMessage (title, result.getErrorMessage());
    }

    void showMessage (const juce::String& title, const juce::String& message)
    {
        showDialog (title, message, { "OK" }, nullptr, nullptr);
    }

    void confirm (const juce::String& title, const juce::String& message, const juce::String& confirmText,
                  std::function<void()> onConfirm)
    {
        showDialog (title, message, { confirmText, "Cancel" }, nullptr,
                    [onConfirm] (int result, const juce::String&) { if (result == 1) onConfirm(); });
    }

    // A text field that has the focus straight away: Enter confirms, Esc cancels.
    void askForName (const juce::String& title, const juce::String& initialName, const juce::String& confirmText,
                     std::function<void (const juce::String&)> onName)
    {
        showDialog (title, {}, { confirmText, "Cancel" }, &initialName,
                    [onName] (int result, const juce::String& text) { if (result == 1) onName (text); });
    }

    // One dialog at a time. It's added to the editor, so it appears over the plugin (never
    // behind the DAW) with Heft's look, and it's asynchronous: nothing blocks the host.
    // The first button confirms (Enter), the last one cancels (Esc).
    void showDialog (const juce::String& title, const juce::String& message, const juce::StringArray& buttons,
                     const juce::String* initialText, std::function<void (int, const juce::String&)> onClose)
    {
        auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
        auto& parent = editor != nullptr ? static_cast<juce::Component&> (*editor) : *getTopLevelComponent();

        closeDialog();
        dialog = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon);
        dialog->setLookAndFeel (&getLookAndFeel());

        if (initialText != nullptr)
            dialog->addTextEditor ("name", *initialText, "Name:");

        for (int i = 0; i < buttons.size(); ++i)
            dialog->addButton (buttons[i], i == 0 ? 1 : 0,
                               i == 0 ? juce::KeyPress (juce::KeyPress::returnKey) : juce::KeyPress(),
                               i == buttons.size() - 1 ? juce::KeyPress (juce::KeyPress::escapeKey) : juce::KeyPress());

        auto* window = dialog.get();
        auto* field = window->getTextEditor ("name");

        if (field != nullptr)
        {
            field->onReturnKey = [window] { window->exitModalState (1); };
            field->onEscapeKey = [window] { window->exitModalState (0); };
        }

        parent.addAndMakeVisible (*window);
        window->setCentrePosition (parent.getLocalBounds().getCentre());

        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<PresetBar> (this), window, onClose] (int result)
            {
                if (safe == nullptr || safe->dialog.get() != window)
                    return;

                const auto text = window->getTextEditorContents ("name");
                safe->closeDialog();

                if (onClose)
                    onClose (result, text);
            }),
            false);

        if (field != nullptr)
        {
            field->grabKeyboardFocus();
            field->selectAll();
        }
    }

    void closeDialog()
    {
        if (dialog == nullptr)
            return;

        // Not deleted right here, because this can run inside the dialog's own callback.
        auto* finished = dialog.release();

        if (finished->isCurrentlyModal())
            finished->exitModalState (0);

        finished->setVisible (false);
        finished->setLookAndFeel (nullptr);
        juce::MessageManager::callAsync ([finished] { delete finished; });
    }

    PresetManager& manager;
    juce::TextButton previousButton, nameButton, nextButton, saveButton { "Save" }, moreButton;
    juce::TextButton slotAButton { "A" }, slotBButton { "B" }, copyButton { "Copy" };

    std::unique_ptr<juce::AlertWindow> dialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
