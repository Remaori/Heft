#pragma once

#include "Palette.h"

// Custom drawing for knobs, switches, buttons, menus, dialogs, text boxes and tooltips.
//
// Knobs are a white disc with a black pointer, inside a thin arc that shows the value.
//   "bipolarFrom" property = value the arc starts from (e.g. 0 dB for Output)
//
// Text buttons are outlined boxes. Properties change what's drawn:
//   "segment" = "left" / "right": half of a two-part box (the A | B selector), filled
//               white while the button's toggle state is on
//   "primary" : filled white (the button that confirms a dialog)
//   "glyph"   = "previous" / "next" / "more": a chevron or three dots instead of text
//   "presetName" (+ "modified"): the preset name box, with an asterisk and a chevron
class HeftLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HeftLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, palette::panel);
        setColour (juce::PopupMenu::textColourId, palette::text);
        setColour (juce::PopupMenu::headerTextColourId, palette::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::text);
        setColour (juce::PopupMenu::highlightedTextColourId, palette::background);
        setColour (juce::AlertWindow::backgroundColourId, palette::panel);
        setColour (juce::AlertWindow::textColourId, palette::text);
        setColour (juce::AlertWindow::outlineColourId, palette::edge);
        setColour (juce::TextButton::textColourOffId, palette::text);
        setColour (juce::TextButton::textColourOnId, palette::background);
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxOutlineColourId, palette::track);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, palette::text.withAlpha (0.3f));
        setColour (juce::Label::textColourId, palette::text);
        setColour (juce::TextEditor::backgroundColourId, palette::background);
        setColour (juce::TextEditor::textColourId, palette::text);
        setColour (juce::TextEditor::highlightColourId, palette::text.withAlpha (0.3f));
        setColour (juce::TextEditor::outlineColourId, palette::edge);
        setColour (juce::TextEditor::focusedOutlineColourId, palette::textDim);
        setColour (juce::ComboBox::outlineColourId, palette::edge);   // AlertWindow outlines its fields with this
        setColour (juce::CaretComponent::caretColourId, palette::text);
        setColour (juce::TooltipWindow::backgroundColourId, palette::panel);
        setColour (juce::TooltipWindow::textColourId, palette::text);
        setColour (juce::TooltipWindow::outlineColourId, palette::edge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds    = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto radius    = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre    = bounds.getCentre();
        const auto arcWidth  = juce::jmax (2.0f, radius * 0.033f);
        const auto arcRadius = radius - arcWidth;
        const auto angle     = startAngle + position * (endAngle - startAngle);

        const auto strokeArc = [&] (float from, float to, juce::Colour colour)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);
            g.setColour (colour);
            g.strokePath (arc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        };

        // Track, then the value arc.
        strokeArc (startAngle, endAngle, palette::track);

        auto from = startAngle;
        const auto& props = slider.getProperties();

        if (props.contains ("bipolarFrom"))
            from = startAngle + (float) slider.valueToProportionOfLength ((double) props["bipolarFrom"])
                                    * (endAngle - startAngle);

        if (std::abs (angle - from) > 0.001f)
            strokeArc (juce::jmin (from, angle), juce::jmax (from, angle), palette::text);

        // Solid disc and pointer.
        const auto discRadius = arcRadius - juce::jmax (6.0f, radius * 0.12f);
        g.setColour (slider.isMouseOverOrDragging() ? juce::Colours::white : palette::knob);
        g.fillEllipse (juce::Rectangle<float> (discRadius * 2.0f, discRadius * 2.0f).withCentre (centre));

        juce::Path pointer;
        pointer.startNewSubPath (centre.getPointOnCircumference (discRadius * 0.35f, angle));
        pointer.lineTo (centre.getPointOnCircumference (discRadius * 0.86f, angle));
        g.setColour (palette::background);
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.5f, discRadius * 0.078f),
                                                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Switch: an outlined box with a square that's filled while it's on.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto height = bounds.getHeight();
        const auto corner = height * 0.11f;

        g.setColour (highlighted ? palette::panel : palette::background);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (palette::edge);
        g.drawRoundedRectangle (bounds, corner, 1.0f);

        const auto size = height * 0.28f;
        const auto square = juce::Rectangle<float> (size, size)
                                .withPosition (bounds.getX() + height * 0.33f, bounds.getCentreY() - size * 0.5f);

        if (button.getToggleState())
        {
            g.setColour (palette::text);
            g.fillRect (square);
        }
        else
        {
            g.setColour (palette::textDim);
            g.drawRect (square.reduced (size * 0.1f), 1.0f);
        }

        g.setColour (palette::text);
        g.setFont (palette::caption (height * 0.36f));
        g.drawText (button.getButtonText().toUpperCase(),
                    bounds.withLeft (square.getRight() + height * 0.28f), juce::Justification::centredLeft, false);
    }

    // Slider value boxes scale their font with their height; other labels keep their own font.
    juce::Font getLabelFont (juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
            return palette::font ((float) label.getHeight() * 0.62f);

        return label.getFont();
    }

    // A plain box, also around the name field of the dialogs (JUCE only underlines those).
    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (! editor.isEnabled())
            return;

        const auto focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
        g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                : juce::TextEditor::outlineColourId));
        g.drawRect (0, 0, width, height);
    }

    void drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override
    {
        g.setColour (palette::textDim.withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.35f));

        for (auto i : { 0.3f, 0.6f })
            g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.0f);
    }

    //==============================================================================
    // Text buttons: outlined boxes; the A | B half that's active and a dialog's confirm
    // button are filled white.
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const auto bounds  = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto& props  = button.getProperties();
        const auto segment = props["segment"].toString();
        const auto corner  = bounds.getHeight() * 0.13f;
        const auto filled  = isFilled (button);

        juce::Path shape;
        shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), corner, corner,
                                   segment != "right", segment != "left", segment != "right", segment != "left");

        auto fill = filled ? palette::text : palette::background;

        if (highlighted || down)
            fill = filled ? juce::Colours::white : palette::panel.brighter (down ? 0.1f : 0.0f);

        g.setColour (button.isEnabled() ? fill : fill.withMultipliedAlpha (0.5f));
        g.fillPath (shape);

        g.setColour (filled ? palette::text : palette::edge);
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
        const auto colour  = isFilled (button) ? palette::background
                           : enabled ? (highlighted ? palette::text : palette::text.withAlpha (0.88f))
                                     : palette::textDim.withAlpha (0.6f);

        if (props.contains ("presetName"))
        {
            drawPresetName (g, bounds, props["presetName"].toString(), (bool) props["modified"], enabled);
            return;
        }

        g.setColour (colour);
        const auto glyph  = props["glyph"].toString();
        const auto size   = bounds.getHeight() * 0.12f;
        const auto centre = bounds.getCentre();

        if (glyph == "previous" || glyph == "next")
        {
            const auto dir = glyph == "next" ? 1.0f : -1.0f;
            juce::Path chevron;
            chevron.startNewSubPath (centre.x - dir * size * 0.5f, centre.y - size);
            chevron.lineTo (centre.x + dir * size * 0.5f, centre.y);
            chevron.lineTo (centre.x - dir * size * 0.5f, centre.y + size);
            g.strokePath (chevron, chevronStroke (bounds.getHeight()));
            return;
        }

        if (glyph == "more")
        {
            const auto dot = bounds.getHeight() * 0.05f;

            for (auto i : { -1.0f, 0.0f, 1.0f })
                g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre ({ centre.x + i * dot * 3.4f, centre.y }));

            return;
        }

        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
    }

    //==============================================================================
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (palette::panel);
        g.setColour (palette::edge);
        g.drawRect (0, 0, width, height);
    }

    // "FACTORY" and "USER" in the preset list, as small captions like the knobs'.
    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override
    {
        g.setColour (findColour (juce::PopupMenu::headerTextColourId));
        g.setFont (palette::caption (12.0f));
        g.drawText (sectionName.toUpperCase(), area.withTrimmedLeft (12).withTrimmedBottom (area.getHeight() / 5),
                    juce::Justification::bottomLeft, false);
    }

    juce::Font getPopupMenuFont() override                     { return palette::font (15.0f); }
    juce::Font getAlertWindowTitleFont() override              { return palette::font (17.0f, true); }
    juce::Font getAlertWindowMessageFont() override            { return palette::font (15.0f); }
    juce::Font getAlertWindowFont() override                   { return palette::font (14.0f); }

private:
    static bool isFilled (juce::Button& button)
    {
        const auto& props = button.getProperties();
        return (props.contains ("segment") && button.getToggleState()) || (bool) props["primary"];
    }

    static juce::PathStrokeType chevronStroke (float buttonHeight)
    {
        return { juce::jmax (1.2f, buttonHeight * 0.053f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded };
    }

    // The preset name box: the name, an asterisk when modified and a small chevron on the
    // right. Long names are cut with "…" and the asterisk stays visible.
    static void drawPresetName (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& name,
                                bool modified, bool enabled)
    {
        const auto height = bounds.getHeight();
        auto area = bounds.reduced (height * 0.4f, 0.0f);

        const auto arrowArea = area.removeFromRight (height * 0.4f);
        const auto a = arrowArea.getCentre();
        const auto s = height * 0.12f;
        juce::Path chevron;
        chevron.startNewSubPath (a.x - s, a.y - s * 0.5f);
        chevron.lineTo (a.x, a.y + s * 0.5f);
        chevron.lineTo (a.x + s, a.y - s * 0.5f);
        g.setColour (palette::textDim);
        g.strokePath (chevron, chevronStroke (height));
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
            g.drawText (asterisk, area.withTrimmedLeft (nameWidth).withWidth (asteriskWidth + 2.0f),
                        juce::Justification::centredLeft, false);
    }

    // Keeps the embedded fonts loaded while this look and feel exists (see palette::font).
    juce::SharedResourcePointer<palette::Typefaces> typefaces;
};
