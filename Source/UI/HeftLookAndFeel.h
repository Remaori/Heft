#pragma once

#include "Palette.h"

// Custom drawing for knobs, switches, buttons, menus, dialogs, text boxes and tooltips.
//
// Knob arcs can be customised per slider through component properties:
//   "arc" = "fatness" (amber that gets hotter), "color" (warm -> bright), "mix" or "neutral"
//   "bipolarFrom" = value the arc starts from (e.g. 0 dB for Output)
//
// Text buttons are pills. Properties change what's drawn:
//   "segment" = "left" / "right": half of a two-part pill (the A | B selector); filled
//               in amber while the button's toggle state is on
//   "glyph"   = "previous" / "next" / "more": a triangle or three dots instead of text
//   "presetName" (+ "modified"): the preset name box, with an amber asterisk and a ▾
class HeftLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HeftLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, palette::panel);
        setColour (juce::PopupMenu::textColourId, palette::text);
        setColour (juce::PopupMenu::headerTextColourId, palette::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::accent.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, palette::text);
        setColour (juce::AlertWindow::backgroundColourId, palette::panel);
        setColour (juce::AlertWindow::textColourId, palette::text);
        setColour (juce::AlertWindow::outlineColourId, palette::panelEdge);
        setColour (juce::TextButton::textColourOffId, palette::text);
        setColour (juce::TextButton::textColourOnId, palette::background);
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, palette::accent.withAlpha (0.35f));
        setColour (juce::Label::textColourId, palette::text);
        setColour (juce::TextEditor::backgroundColourId, palette::panel);
        setColour (juce::TextEditor::textColourId, palette::text);
        setColour (juce::TextEditor::highlightColourId, palette::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId, palette::panelEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, palette::accent);
        setColour (juce::CaretComponent::caretColourId, palette::accent);
        setColour (juce::TooltipWindow::backgroundColourId, palette::panel);
        setColour (juce::TooltipWindow::textColourId, palette::text);
        setColour (juce::TooltipWindow::outlineColourId, palette::panelEdge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds    = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto radius    = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre    = bounds.getCentre();
        const auto arcWidth  = juce::jmax (3.0f, radius * 0.085f);
        const auto arcRadius = radius - arcWidth * 1.3f;   // leaves room for the glow
        const auto angle     = startAngle + position * (endAngle - startAngle);

        const auto strokeArc = [&] (float from, float to, juce::Colour colour, float widthScale)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);
            g.setColour (colour);
            g.strokePath (arc, juce::PathStrokeType (arcWidth * widthScale, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        };

        // Track, then the value arc with a soft glow underneath.
        strokeArc (startAngle, endAngle, palette::track, 1.0f);

        auto from = startAngle;
        const auto& props = slider.getProperties();
        const auto colour = arcColour (slider, position);

        if (props.contains ("bipolarFrom"))
        {
            from = startAngle + (float) slider.valueToProportionOfLength ((double) props["bipolarFrom"])
                                    * (endAngle - startAngle);

            // Small mark where the arc starts (e.g. 0 dB).
            const auto mark = centre.getPointOnCircumference (arcRadius, from);
            g.setColour (palette::textDim);
            g.fillEllipse (juce::Rectangle<float> (arcWidth * 0.6f, arcWidth * 0.6f).withCentre (mark));
        }

        if (std::abs (angle - from) > 0.001f)
        {
            strokeArc (juce::jmin (from, angle), juce::jmax (from, angle), colour.withAlpha (0.14f), 2.2f);
            strokeArc (juce::jmin (from, angle), juce::jmax (from, angle), colour, 1.0f);
        }

        // Knob body.
        const auto bodyRadius = arcRadius - arcWidth * 1.7f;
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (body.translated (0.0f, bodyRadius * 0.06f).expanded (bodyRadius * 0.04f));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f49), centre.x, body.getY(),
                                                 juce::Colour (0xff1c1f25), centre.x, body.getBottom(), false));
        g.fillEllipse (body);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        // Pointer.
        juce::Path pointer;
        pointer.startNewSubPath (centre.getPointOnCircumference (bodyRadius * 0.38f, angle));
        pointer.lineTo (centre.getPointOnCircumference (bodyRadius * 0.86f, angle));
        g.setColour (slider.isMouseOverOrDragging() ? colour.brighter (0.3f) : palette::text);
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.0f, bodyRadius * 0.075f),
                                                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pill-shaped switch with an LED.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto on     = button.getToggleState();
        const auto corner = bounds.getHeight() * 0.5f;

        g.setColour (on ? palette::panel.brighter (0.05f) : palette::panel);
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (on ? palette::accent.withAlpha (0.55f)
                        : palette::panelEdge.brighter (highlighted ? 0.25f : 0.0f));
        g.drawRoundedRectangle (bounds, corner, 1.0f);

        const auto ledSize = bounds.getHeight() * 0.32f;
        const auto led = juce::Rectangle<float> (ledSize, ledSize)
                             .withCentre ({ bounds.getX() + corner, bounds.getCentreY() });

        if (on)
        {
            g.setColour (palette::accent.withAlpha (0.25f));
            g.fillEllipse (led.expanded (ledSize * 0.5f));
            g.setColour (palette::accent);
        }
        else
        {
            g.setColour (palette::track.brighter (0.2f));
        }

        g.fillEllipse (led);

        g.setColour (on ? palette::text : palette::textDim);
        g.setFont (palette::caption (bounds.getHeight() * 0.36f));
        g.drawText (button.getButtonText().toUpperCase(),
                    bounds.withTrimmedLeft (corner * 2.0f).withTrimmedRight (corner * 0.5f),
                    juce::Justification::centredLeft, false);
    }

    // Slider value boxes scale their font with their height; other labels keep their own font.
    juce::Font getLabelFont (juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
            return palette::font ((float) label.getHeight() * 0.62f);

        return label.getFont();
    }

    void drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override
    {
        g.setColour (palette::textDim.withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.35f));

        for (auto i : { 0.3f, 0.6f })
            g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.0f);
    }

    //==============================================================================
    // Text buttons: pills like the Clipper and Level Match switches.
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const auto bounds  = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto segment = button.getProperties()["segment"].toString();
        const auto corner  = bounds.getHeight() * 0.5f;
        const auto active  = segment.isNotEmpty() && button.getToggleState();

        juce::Path shape;
        shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), corner, corner,
                                   segment != "right", segment != "left", segment != "right", segment != "left");

        auto fill = active ? palette::accent : palette::panel;

        if (! active && (highlighted || down))
            fill = fill.brighter (down ? 0.12f : 0.07f);

        g.setColour (button.isEnabled() ? fill : fill.withMultipliedAlpha (0.5f));
        g.fillPath (shape);

        g.setColour (active ? palette::accent : palette::panelEdge.brighter (highlighted && button.isEnabled() ? 0.25f : 0.0f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return palette::font ((float) buttonHeight * 0.48f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool) override
    {
        const auto& props  = button.getProperties();
        const auto bounds  = button.getLocalBounds().toFloat();
        const auto enabled = button.isEnabled();
        const auto colour  = (button.getToggleState() && props.contains ("segment")) ? palette::background
                           : enabled ? (highlighted ? palette::text : palette::text.withAlpha (0.88f))
                                     : palette::textDim.withAlpha (0.6f);

        if (props.contains ("presetName"))
        {
            drawPresetName (g, bounds, props["presetName"].toString(), (bool) props["modified"], enabled);
            return;
        }

        g.setColour (colour);
        const auto glyph = props["glyph"].toString();
        const auto size  = bounds.getHeight() * 0.17f;
        const auto centre = bounds.getCentre();

        if (glyph == "previous" || glyph == "next")
        {
            const auto dir = glyph == "next" ? 1.0f : -1.0f;
            juce::Path triangle;
            triangle.addTriangle (centre.x - dir * size * 0.55f, centre.y - size,
                                  centre.x - dir * size * 0.55f, centre.y + size,
                                  centre.x + dir * size * 0.75f, centre.y);
            g.fillPath (triangle);
            return;
        }

        if (glyph == "more")
        {
            const auto dot = bounds.getHeight() * 0.055f;

            for (auto i : { -1.0f, 0.0f, 1.0f })
                g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre ({ centre.x + i * dot * 3.2f, centre.y }));

            return;
        }

        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
    }

    //==============================================================================
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (palette::panel);
        g.setColour (palette::panelEdge.brighter (0.2f));
        g.drawRect (0, 0, width, height);
    }

    juce::Font getPopupMenuFont() override                     { return palette::font (15.0f); }
    juce::Font getAlertWindowTitleFont() override              { return palette::font (17.0f, true); }
    juce::Font getAlertWindowMessageFont() override            { return palette::font (15.0f); }
    juce::Font getAlertWindowFont() override                   { return palette::font (14.0f); }

private:
    // The preset name box: name in the main colour, an amber asterisk when modified,
    // and a small ▾ on the right. Long names are cut with "…".
    static void drawPresetName (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& name,
                                bool modified, bool enabled)
    {
        const auto height = bounds.getHeight();
        auto area = bounds.reduced (height * 0.5f, 0.0f);

        const auto arrowArea = area.removeFromRight (height * 0.5f);
        juce::Path arrow;
        const auto a = arrowArea.getCentre();
        const auto s = height * 0.11f;
        arrow.addTriangle (a.x - s * 1.3f, a.y - s * 0.6f, a.x + s * 1.3f, a.y - s * 0.6f, a.x, a.y + s);
        g.setColour (palette::textDim);
        g.fillPath (arrow);
        area.removeFromRight (height * 0.2f);

        const auto font = palette::font (height * 0.5f);
        g.setFont (font);

        const juce::String asterisk (modified ? " *" : "");
        const auto asteriskWidth = juce::GlyphArrangement::getStringWidth (font, asterisk);
        const auto nameWidth = juce::jmin (juce::GlyphArrangement::getStringWidth (font, name),
                                           area.getWidth() - asteriskWidth);

        g.setColour (enabled ? palette::text : palette::textDim);
        g.drawText (name, area.withWidth (nameWidth + 1.0f), juce::Justification::centredLeft, true);

        if (modified)
        {
            g.setColour (palette::accent);
            g.drawText (asterisk, area.withTrimmedLeft (nameWidth).withWidth (asteriskWidth + 2.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    static juce::Colour arcColour (juce::Slider& slider, float position)
    {
        const auto style = slider.getProperties()["arc"].toString();

        if (style == "color")
            return palette::warm.interpolatedWith (palette::bright, position);

        if (style == "neutral")
            return palette::neutral;

        if (style == "mix")
            return palette::mix;

        return palette::accent.interpolatedWith (palette::accentHot, position * position);
    }
};
