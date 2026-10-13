#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours and fonts shared by the whole UI. Change them here to restyle Heft.
namespace palette
{
    inline const juce::Colour background   { 0xff15171c };
    inline const juce::Colour backgroundLo { 0xff101216 };   // bottom of the background gradient
    inline const juce::Colour panel        { 0xff1d2027 };
    inline const juce::Colour panelEdge    { 0xff2b2f38 };
    inline const juce::Colour track        { 0xff2a2e36 };   // unlit part of knob arcs and meters
    inline const juce::Colour text         { 0xffe9e6e1 };
    inline const juce::Colour textDim      { 0xff8b909a };

    inline const juce::Colour accent       { 0xffff9a3d };   // amber: Fatness, switches
    inline const juce::Colour accentHot    { 0xffff5f38 };   // Fatness at 100 %
    inline const juce::Colour warm         { 0xffc9573a };   // Color at 0 % (dark / warm)
    inline const juce::Colour bright       { 0xffffe49a };   // Color at 100 % (bright / aggressive)
    inline const juce::Colour neutral      { 0xffb8c4d6 };   // Output

    inline const juce::Colour meterLow     { 0xff4fc99a };
    inline const juce::Colour meterMid     { 0xffffc547 };
    inline const juce::Colour meterHigh    { 0xffff4d3d };

    // Segoe UI ships with every Windows 10/11 install.
    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Small uppercase captions with extra letter spacing.
    inline juce::Font caption (float height)
    {
        return font (height, true).withExtraKerningFactor (0.12f);
    }
}
