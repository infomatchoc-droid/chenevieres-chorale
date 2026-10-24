// Couleurs, polices et dessin des contrôles (d'après la maquette validée le 24/10/2026)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace theme
{
inline const juce::Colour bg       { 0xff16171a };
inline const juce::Colour header   { 0xff1b1c20 };
inline const juce::Colour border   { 0xff2a2c32 };
inline const juce::Colour panel    { 0xff1c1d21 };
inline const juce::Colour panelSel { 0xff23252b };
inline const juce::Colour field    { 0xff222329 };
inline const juce::Colour fieldBd  { 0xff34363d };
inline const juce::Colour stage    { 0xff111215 };
inline const juce::Colour grid     { 0xff1c1d22 };
inline const juce::Colour text     { 0xffece8e1 };
inline const juce::Colour muted    { 0xff9a9da6 };
inline const juce::Colour dim      { 0xff6e717a };
inline const juce::Colour accent   { 0xffe2b04a };
inline const juce::Colour ink      { 0xff16171a };   // texte sur fond doré

inline juce::Colour section (int s)
{
    static const juce::Colour c[4] = { juce::Colour (0xfff2896b), juce::Colour (0xffe9c45a), juce::Colour (0xff57c2b6), juce::Colour (0xff7c95e6) };
    return c[juce::jlimit (0, 3, s)];
}

inline juce::Typeface::Ptr cinzel()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::CinzelRegular_ttf, BinaryData::CinzelRegular_ttfSize);
    return t;
}
inline juce::Font ui (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
inline juce::Font mono (float h) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); }
inline juce::Font title (float h) { return juce::Font (juce::FontOptions (cinzel()).withHeight (h)); }

// Petit titre de panneau en capitales espacées
inline void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (muted);
    g.setFont (ui (11.0f, true).withExtraKerningFactor (0.12f));
    g.drawText (s, r, j, false);   // déjà en capitales (toUpperCase ne gère pas les accents)
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::ComboBox::backgroundColourId, field);
        setColour (juce::ComboBox::outlineColourId, fieldBd);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, muted);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff202126));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::PopupMenu::headerTextColourId, muted);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff26282e));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, fieldBd);
        setColour (juce::TextButton::buttonColourId, field);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, muted);
        setColour (juce::TextButton::textColourOnId, ink);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return ui (13.0f); }
    juce::Font getPopupMenuFont() override { return ui (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return ui (juce::jmin (13.0f, (float) h * 0.5f), true); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        juce::Path p;
        const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        p.startNewSubPath (cx - 4, cy - 2); p.lineTo (cx, cy + 2); p.lineTo (cx + 4, cy - 2);
        g.setColour (muted);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float rad = (float) b.getProperties().getWithDefault ("radius", 6.0);
        if (on)
        {
            g.setColour (b.findColour (juce::TextButton::buttonOnColourId));
            g.fillRoundedRectangle (r, rad);
        }
        else if (! b.getProperties().getWithDefault ("flat", false))
        {
            g.setColour (b.findColour (juce::TextButton::buttonColourId).brighter (down ? 0.15f : (over ? 0.07f : 0.0f)));
            g.fillRoundedRectangle (r, rad);
            g.setColour (fieldBd);
            g.drawRoundedRectangle (r, rad, 1.0f);
        }
        else if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, rad);
        }
    }

    // Interrupteur (pupitre actif, tessiture automatique)
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto bounds = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const juce::Colour col = b.findColour (juce::ToggleButton::tickColourId);
        auto sw = bounds.removeFromRight (30.0f).withSizeKeepingCentre (30.0f, 16.0f);
        g.setColour (on ? col : juce::Colour (0xff3a3c43).brighter (over ? 0.1f : 0.0f));
        g.fillRoundedRectangle (sw, 8.0f);
        const float kx = on ? sw.getRight() - 14.0f : sw.getX() + 2.0f;
        g.setColour (on ? ink : text.withAlpha (0.85f));
        g.fillEllipse (kx, sw.getY() + 2.0f, 12.0f, 12.0f);
        if (b.getButtonText().isNotEmpty())
        {
            g.setColour (muted);
            g.setFont (ui (12.0f));
            g.drawText (b.getButtonText(), bounds.withTrimmedRight (6.0f), juce::Justification::centredRight, false);
        }
    }

    // Bouton rotatif : arc de 270°, valeur en doré
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        const auto c = r.withSizeKeepingCentre (size, size);
        const float rad = size * 0.5f - 3.0f;
        const juce::Colour col = s.findColour (juce::Slider::rotarySliderFillColourId);
        juce::Path track, val;
        track.addCentredArc (c.getCentreX(), c.getCentreY(), rad, rad, 0, a0, a1, true);
        g.setColour (juce::Colour (0xff2e3036));
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        const float av = a0 + pos * (a1 - a0);
        // bipolaire (formants) : l'arc part du centre
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        if (std::abs (av - from) > 0.001f)
        {
            val.addCentredArc (c.getCentreX(), c.getCentreY(), rad, rad, 0, juce::jmin (from, av), juce::jmax (from, av), true);
            g.setColour (col);
            g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        const auto inner = c.reduced (8.0f);
        g.setColour (juce::Colour (0xff1e1f24));
        g.fillEllipse (inner);
        g.setColour (fieldBd);
        g.drawEllipse (inner, 1.0f);
        const float pr = inner.getWidth() * 0.5f - 5.0f;
        g.setColour (text);
        g.fillEllipse (c.getCentreX() + pr * std::sin (av) - 2.0f, c.getCentreY() - pr * std::cos (av) - 2.0f, 4.0f, 4.0f);
    }

    // Glissière « barre » des pupitres
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f, (float) w, 4.0f);
        g.setColour (border);
        g.fillRoundedRectangle (r, 2.0f);
        const juce::Colour col = s.findColour (juce::Slider::trackColourId);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? r.getCentreX() : r.getX();
        const float to = juce::jlimit (r.getX(), r.getRight(), pos);
        g.setColour (col);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (from, to), r.getY(), std::abs (to - from), 4.0f), 2.0f);
        g.setColour (s.isMouseOverOrDragging() ? text : col.brighter (0.2f));
        g.fillEllipse (to - 4.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (fieldBd);
        g.drawRect (0, 0, w, h);
    }
};
} // namespace theme
