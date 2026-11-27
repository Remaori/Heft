#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HeftFonts.h"

// Colours and fonts shared by the whole UI. Change them here to restyle Heft.
// Black and white: a near-black and an off-white glare less than pure #000 and #fff.
namespace palette
{
    inline const juce::Colour background { 0xff0d0d0d };
    inline const juce::Colour panel      { 0xff161616 };   // menus, dialogs, tooltips, hover
    inline const juce::Colour divider    { 0xff232323 };   // the lines under the header and between columns
    inline const juce::Colour track      { 0xff262626 };   // unlit part of knob arcs and meters, value boxes
    inline const juce::Colour edge       { 0xff383838 };   // outlines of buttons, switches and menus
    inline const juce::Colour textDim    { 0xff8c8c8c };
    inline const juce::Colour text       { 0xfff0f0f0 };   // also lit arcs and meters, and "on" fills
    inline const juce::Colour knob       { 0xffececec };

    // Inter, compiled into the plugin so it looks the same on Windows and macOS. The look
    // and feel keeps these loaded while an editor is open; the last editor to close frees them.
    struct Typefaces
    {
        juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (HeftFonts::InterMedium_ttf,
                                                                              (size_t) HeftFonts::InterMedium_ttfSize);
        juce::Typeface::Ptr bold   = juce::Typeface::createSystemTypefaceFor (HeftFonts::InterBold_ttf,
                                                                              (size_t) HeftFonts::InterBold_ttfSize);
    };

    inline juce::Font font (float height, bool bold = false)
    {
        const juce::SharedResourcePointer<Typefaces> typefaces;
        return juce::Font (juce::FontOptions (bold ? typefaces->bold : typefaces->medium).withHeight (height));
    }

    // Small uppercase captions with extra letter spacing.
    inline juce::Font caption (float height)
    {
        return font (height, true).withExtraKerningFactor (0.12f);
    }
}
