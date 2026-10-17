#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

// Reading and writing .heftpreset files, plus name validation. Pure functions with
// no UI and no state, so they can be tested on their own.
//
// File format (UTF-8 XML, editable in Notepad):
//
//   <HeftPreset formatVersion="1" pluginVersion="0.4.0">
//     <Param id="fatness" value="62.0"/>
//     ...
//   </HeftPreset>
//
// Values are in real units (62.0 for 62 %, -3.0 for dB, 0 or 1 for switches). The
// preset's name is the file name, so renaming a file in Explorer just works.
namespace PresetFile
{
    inline constexpr size_t numParams = presetParamIds.size();

    // One value per presetParamIds entry, same order, in real units.
    using Values = std::array<float, numParams>;

    inline constexpr int formatVersion = 1;
    inline constexpr juce::int64 maxFileBytes = 64 * 1024;   // a preset is < 1 KB; refuse anything huge
    inline constexpr int maxNameLength = 64;
    inline const juce::String extension { ".heftpreset" };
    inline const juce::String tempExtension { ".heftpreset-tmp" };   // a save in progress
    inline const juce::String rootTag { "HeftPreset" };

    // Range and default of each preset parameter, taken from the plugin's own
    // parameters so nothing here can drift from Parameters.h.
    struct ParamSpec
    {
        juce::String id;
        float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
        bool isSwitch = false;
    };

    using Specs = std::array<ParamSpec, numParams>;

    inline Specs makeSpecs (juce::AudioProcessorValueTreeState& apvts)
    {
        Specs specs;

        for (size_t i = 0; i < numParams; ++i)
        {
            auto* param = apvts.getParameter (presetParamIds[i]);
            jassert (param != nullptr);

            const auto& range = param->getNormalisableRange();
            specs[i] = { presetParamIds[i], range.start, range.end,
                         param->convertFrom0to1 (param->getDefaultValue()),
                         dynamic_cast<juce::AudioParameterBool*> (param) != nullptr };
        }

        return specs;
    }

    inline Values defaults (const Specs& specs)
    {
        Values values {};

        for (size_t i = 0; i < numParams; ++i)
            values[i] = specs[i].defaultValue;

        return values;
    }

    // Out-of-range values are clamped; switches are on at 0.5 or more.
    inline float sanitise (const ParamSpec& spec, float value)
    {
        if (! std::isfinite (value))
            return spec.defaultValue;

        if (spec.isSwitch)
            return value >= 0.5f ? 1.0f : 0.0f;

        return juce::jlimit (spec.minValue, spec.maxValue, value);
    }

    // True when every value is within half a knob step (0.05) and every switch matches.
    // Used for the "modified" asterisk.
    inline bool matches (const Values& a, const Values& b, const Specs& specs)
    {
        for (size_t i = 0; i < numParams; ++i)
        {
            if (specs[i].isSwitch ? ((a[i] >= 0.5f) != (b[i] >= 0.5f))
                                  : std::abs (a[i] - b[i]) > 0.05f)
                return false;
        }

        return true;
    }

    // Always with a dot, whatever the Windows regional settings (juce::String formats
    // numbers with the "C" locale).
    inline juce::String formatValue (const ParamSpec& spec, float value)
    {
        if (spec.isSwitch)
            return value >= 0.5f ? "1" : "0";

        return juce::String (juce::roundToInt (value * 10.0f) / 10.0, 1);
    }

    inline juce::String toXml (const Values& values, const Specs& specs, const juce::String& pluginVersion)
    {
        juce::XmlElement root (rootTag);
        root.setAttribute ("formatVersion", formatVersion);
        root.setAttribute ("pluginVersion", pluginVersion);

        for (size_t i = 0; i < numParams; ++i)
        {
            auto* param = root.createNewChildElement ("Param");
            param->setAttribute ("id", specs[i].id);
            param->setAttribute ("value", formatValue (specs[i], values[i]));
        }

        return root.toString();
    }

    // Parameters are matched by id, never by position. Missing ones get their default,
    // unknown ones are ignored, out-of-range values are clamped.
    inline juce::Result fromXml (const juce::String& text, const Specs& specs, Values& out)
    {
        const auto xml = juce::XmlDocument::parse (text);

        if (xml == nullptr || ! xml->hasTagName (rootTag))
            return juce::Result::fail ("is not a Heft preset.");

        if (xml->getIntAttribute ("formatVersion", formatVersion) > formatVersion)
            return juce::Result::fail ("was made with a newer version of Heft.");

        auto values = defaults (specs);

        for (auto* param : xml->getChildWithTagNameIterator ("Param"))
        {
            const auto id = param->getStringAttribute ("id");

            for (size_t i = 0; i < numParams; ++i)
                if (id == specs[i].id && param->hasAttribute ("value"))
                    values[i] = sanitise (specs[i], (float) param->getStringAttribute ("value").getDoubleValue());
        }

        out = values;
        return juce::Result::ok();
    }

    // Reads and validates a file. The error text completes a sentence that starts
    // with the file name ("X is not a Heft preset.").
    inline juce::Result read (const juce::File& file, const Specs& specs, Values& out)
    {
        if (! file.existsAsFile())
            return juce::Result::fail ("no longer exists.");

        if (file.getSize() > maxFileBytes)
            return juce::Result::fail ("is not a Heft preset (the file is too big).");

        return fromXml (file.loadFileAsString(), specs, out);
    }

    // Atomic write: to a temporary file first, then it replaces the target. A failure
    // half-way never leaves a broken preset behind.
    //
    // The temporary file is ".Name.<random>.heftpreset-tmp" in the same folder: the same
    // disk, so replacing is a rename, and it doesn't end in .heftpreset, so a save cut
    // short (Heft or the DAW closing mid-save) never shows up as a broken preset. Old
    // leftovers are removed by PresetManager::refreshList().
    inline juce::Result write (const juce::File& file, const Values& values, const Specs& specs,
                               const juce::String& pluginVersion)
    {
        const auto temp = file.getSiblingFile ("." + file.getFileNameWithoutExtension() + "."
                                               + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
                                               + tempExtension);

        const auto fail = [&] (const juce::String& reason)
        {
            temp.deleteFile();
            return juce::Result::fail ("Couldn't save \"" + file.getFullPathName() + "\"" + reason);
        };

        {
            juce::FileOutputStream out (temp);

            if (! out.openedOk())
                return fail (": " + out.getStatus().getErrorMessage());

            out.writeText (toXml (values, specs, pluginVersion), false, false, "\r\n");
            out.flush();

            if (out.getStatus().failed())
                return fail (": " + out.getStatus().getErrorMessage());
        }

        // A few tries: antivirus or OneDrive can hold the target for a moment.
        for (int attempt = 1; attempt <= 3; ++attempt)
        {
            if (temp.replaceFileIn (file))
                return juce::Result::ok();

            if (attempt < 3)
                juce::Thread::sleep (100);
        }

        return fail (". The file may be read-only or in use by another program.");
    }

    // Returns an empty string if the name can be used for a file, otherwise why not.
    inline juce::String validateName (const juce::String& name)
    {
        if (name.isEmpty())
            return "Please enter a name.";

        if (name != name.trim())
            return "The name can't start or end with a space.";

        if (name.length() > maxNameLength)
            return "The name can be at most " + juce::String (maxNameLength) + " characters long.";

        if (name.containsAnyOf ("\\/:*?\"<>|"))
            return "The name can't contain any of these characters: \\ / : * ? \" < > |";

        for (auto c : name)
            if (c < 32)
                return "The name contains characters that can't be used in a file name.";

        if (name.endsWithChar ('.'))
            return "The name can't end with a period.";

        static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL",
                                                  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                                  "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };

        if (reserved.contains (name.upToFirstOccurrenceOf (".", false, false), true))
            return "\"" + name + "\" is a name Windows reserves. Please choose another one.";

        return {};
    }

    // Factory files are named "01 Kick.heftpreset" so they sort in a chosen order;
    // the leading number (and the space or dash after it) isn't shown.
    inline juce::String factoryDisplayName (const juce::String& fileName)
    {
        const auto name = fileName.upToLastOccurrenceOf (".", false, false);
        const auto digits = name.initialSectionContainingOnly ("0123456789");

        if (digits.isNotEmpty() && name.length() > digits.length())
        {
            const auto separator = name[digits.length()];

            if (separator == ' ' || separator == '-')
            {
                const auto rest = name.substring (digits.length()).trimCharactersAtStart (" -");

                if (rest.isNotEmpty())
                    return rest;
            }
        }

        return name;
    }
}
