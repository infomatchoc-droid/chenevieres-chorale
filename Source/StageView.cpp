#include "StageView.h"
#include "ParamIDs.h"
#include "Theme.h"

StageView::StageView (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (20);
}

StageView::~StageView() { stopTimer(); }

float StageView::value (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

bool StageView::sectionVisible (int s) const
{
    if (s == 3 && value (ids::voices) < 0.5f) return false;
    return value (ids::sec (s, ids::on)) > 0.5f;
}

int StageView::singersPerSection() const
{
    const int N = choir::kSizes[juce::jlimit (0, choir::kNumSizes - 1, (int) value (ids::size))];
    int n = 0;
    for (int s = 0; s < choir::kSections; ++s) if (sectionVisible (s)) ++n;
    return juce::jmax (1, (int) std::lround ((double) N / juce::jmax (1, n)));
}

float StageView::stageWidthMetres() const
{
    const int N = choir::kSizes[juce::jlimit (0, choir::kNumSizes - 1, (int) value (ids::size))];
    return juce::jlimit (4.0f, 42.0f, 3.5f + 1.25f * std::sqrt ((float) N));
}

juce::Rectangle<float> StageView::floorArea() const
{
    return getLocalBounds().toFloat().reduced (40.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (74.0f);
}

juce::Point<float> StageView::centreOf (int s) const
{
    const auto a = floorArea();
    return { a.getX() + value (ids::sec (s, ids::x)) * a.getWidth(), a.getY() + value (ids::sec (s, ids::y)) * a.getHeight() };
}

float StageView::radiusPx (int s) const
{
    // même géométrie que le moteur (ChoirEngine::updateGeometry)
    const int per = singersPerSection();
    const float spread = value (ids::sec (s, ids::spread)) * 0.01f;
    const float R = std::sqrt ((float) per * 0.55f / juce::MathConstants<float>::pi) * (0.45f + 1.1f * spread) + 0.15f;
    return R / stageWidthMetres() * floorArea().getWidth();
}

int StageView::hitSection (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 1e9f;
    for (int s = 0; s < choir::kSections; ++s)
    {
        if (! sectionVisible (s)) continue;
        const auto c = centreOf (s);
        const float rx = juce::jmax (22.0f, radiusPx (s)), ry = juce::jmax (18.0f, rx * 0.62f);
        const float dx = (p.x - c.x) / rx, dy = (p.y - c.y + 8.0f) / (ry + 14.0f);
        const float d = dx * dx + dy * dy;
        if (d < 1.0f && d < bestD) { bestD = d; best = s; }
    }
    return best;
}

juce::int64 StageView::stateHash() const
{
    juce::int64 h = 1469598103934665603LL;
    auto mix = [&h] (float v) { h = (h ^ (juce::int64) std::lround (v * 1000.0f)) * 1099511628211LL; };
    mix (value (ids::size)); mix (value (ids::voices)); mix (value (ids::distance)); mix ((float) selected); mix ((float) hover);
    for (int s = 0; s < choir::kSections; ++s)
        for (auto* w : { ids::on, ids::interval, ids::spread, ids::x, ids::y })
            mix (value (ids::sec (s, w)));
    return h;
}

void StageView::timerCallback()
{
    const auto h = stateHash();
    if (h != lastHash) { lastHash = h; repaint(); }
}

void StageView::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto b = getLocalBounds().toFloat();
    g.setColour (stage);
    g.fillRoundedRectangle (b, 14.0f);

    // grille de sol
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (b, 14.0f);
        g.reduceClipRegion (clip);
        g.setColour (grid);
        for (float x = b.getX() + 40.0f; x < b.getRight(); x += 40.0f) g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
        for (float y = b.getY() + 40.0f; y < b.getBottom(); y += 40.0f) g.drawHorizontalLine ((int) y, b.getX(), b.getRight());
    }
    g.setColour (border);
    g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.0f);

    // contour de la salle (abside en haut)
    const auto a = floorArea();
    {
        juce::Path hall;
        const float r = a.getWidth() * 0.5f;
        const float top = a.getY() - 6.0f, bottom = a.getBottom() + 22.0f;
        hall.startNewSubPath (a.getX(), bottom);
        hall.lineTo (a.getX(), juce::jmin (bottom - 10.0f, top + r * 0.55f));
        hall.quadraticTo (a.getX(), top, a.getCentreX(), top);
        hall.quadraticTo (a.getRight(), top, a.getRight(), juce::jmin (bottom - 10.0f, top + r * 0.55f));
        hall.lineTo (a.getRight(), bottom);
        g.setColour (fieldBd);
        g.strokePath (hall, juce::PathStrokeType (1.0f));
    }
    g.setColour (dim);
    g.setFont (ui (11.0f).withExtraKerningFactor (0.1f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("FOND DE LA SALLE")), a.withHeight (14.0f).translated (0, 6.0f), juce::Justification::centred);

    // choristes
    const int per = singersPerSection();
    const int N = choir::kSizes[juce::jlimit (0, choir::kNumSizes - 1, (int) value (ids::size))];
    const int shown = juce::jmin (per, 90);
    const float dot = N >= 600 ? 4.0f : (N >= 300 ? 5.0f : (N >= 50 ? 6.0f : 8.0f));
    for (int s = 0; s < choir::kSections; ++s)
    {
        if (! sectionVisible (s)) continue;
        const auto c = centreOf (s);
        const float rx = radiusPx (s), ry = rx * 0.62f;
        const bool sel = s == selected;
        const auto col = section (s);
        if (sel || s == hover)
        {
            g.setColour (col.withAlpha (sel ? 0.10f : 0.06f));
            g.fillEllipse (c.x - rx - 10.0f, c.y - ry - 8.0f, 2 * rx + 20.0f, 2 * ry + 16.0f);
        }
        juce::Random rng (1234 + s * 77);
        g.setColour (col.withAlpha (sel ? 0.95f : 0.62f));
        for (int i = 0; i < shown; ++i)
        {
            const float rad = shown == 1 ? 0.0f : std::sqrt (((float) i + 0.5f) / (float) shown);
            const float ang = (float) i * 2.39996f + (float) s;
            const float jx = (rng.nextFloat() - 0.5f) * 0.12f, jy = (rng.nextFloat() - 0.5f) * 0.12f;
            const float px = c.x + (rad * std::cos (ang) + jx) * rx, py = c.y + (rad * std::sin (ang) + jy) * ry;
            g.fillEllipse (px - dot * 0.5f, py - dot * 0.5f, dot, dot);
        }

        // étiquette
        const auto txt = names::sectionShort()[s] + juce::String (juce::CharPointer_UTF8 (" · "))
                         + names::intervals()[(int) value (ids::sec (s, ids::interval))];
        const auto f = ui (12.0f, true);
        const float tw = juce::GlyphArrangement::getStringWidth (f, txt) + 22.0f;
        auto tag = juce::Rectangle<float> (tw, 24.0f).withCentre ({ c.x, c.y - ry - 20.0f });
        tag = tag.withX (juce::jlimit (b.getX() + 4.0f, b.getRight() - tw - 4.0f, tag.getX())).withY (juce::jmax (b.getY() + 4.0f, tag.getY()));
        g.setColour (sel ? col : header);
        g.fillRoundedRectangle (tag, 12.0f);
        g.setColour (col);
        g.drawRoundedRectangle (tag.reduced (0.5f), 12.0f, 1.0f);
        g.setColour (sel ? ink : text);
        g.setFont (f);
        g.drawText (txt, tag, juce::Justification::centred, false);
    }

    // voix principale / auditeur
    const juce::Point<float> me { b.getCentreX(), b.getBottom() - 46.0f };
    g.setColour (text.withAlpha (0.12f));
    g.fillEllipse (me.x - 15.0f, me.y - 15.0f, 30.0f, 30.0f);
    g.setColour (text);
    g.fillEllipse (me.x - 9.0f, me.y - 9.0f, 18.0f, 18.0f);
    g.setColour (juce::Colour (0xffc9c5be));
    g.setFont (ui (11.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("Voix principale · auditeur à ")) + juce::String (juce::roundToInt (value (ids::distance))) + " m",
                juce::Rectangle<float> (300.0f, 16.0f).withCentre ({ me.x, me.y + 26.0f }), juce::Justification::centred);

    g.setColour (dim);
    g.setFont (mono (11.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("largeur ≈ ")) + juce::String (juce::roundToInt (stageWidthMetres())) + " m",
                b.reduced (14.0f, 10.0f), juce::Justification::bottomRight);
}

void StageView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitSection (e.position);
    if (h != hover) { hover = h; setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); }
}

void StageView::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitSection (e.position);
    if (dragging < 0) return;
    selected = dragging;
    if (onSelect) onSelect (dragging);
    dragOffset = centreOf (dragging) - e.position;
    for (auto* w : { ids::x, ids::y })
        if (auto* p = apvts.getParameter (ids::sec (dragging, w))) p->beginChangeGesture();
    repaint();
}

void StageView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto a = floorArea();
    const auto pos = e.position + dragOffset;
    const float x = juce::jlimit (0.02f, 0.98f, (pos.x - a.getX()) / a.getWidth());
    const float y = juce::jlimit (0.02f, 1.0f, (pos.y - a.getY()) / a.getHeight());
    if (auto* p = apvts.getParameter (ids::sec (dragging, ids::x))) p->setValueNotifyingHost (p->convertTo0to1 (x));
    if (auto* p = apvts.getParameter (ids::sec (dragging, ids::y))) p->setValueNotifyingHost (p->convertTo0to1 (y));
}

void StageView::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;
    for (auto* w : { ids::x, ids::y })
        if (auto* p = apvts.getParameter (ids::sec (dragging, w))) p->endChangeGesture();
    dragging = -1;
}

void StageView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-clic : le pupitre revient à sa place d'origine
    const int s = hitSection (e.position);
    if (s < 0) return;
    for (auto* w : { ids::x, ids::y })
        if (auto* p = apvts.getParameter (ids::sec (s, w)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}
