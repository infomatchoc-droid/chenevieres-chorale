#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Presets.h"
#include "MidiArranger.h"

using namespace theme;
using names::u;

//==============================================================================
ChoiceButtons::ChoiceButtons (APVTS& state, const juce::String& paramId, const juce::StringArray& labels, bool seg)
    : segmented (seg)
{
    auto* param = state.getParameter (paramId);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->getProperties().set ("flat", segmented);
        b->getProperties().set ("radius", segmented ? 6.0 : 7.0);
        b->onClick = [this, i, param] {
            if (attachment != nullptr) attachment->setValueAsCompleteGesture ((float) i);
            juce::ignoreUnused (param);
        };
        addAndMakeVisible (b);
    }
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) {
        const int idx = (int) std::lround (v);
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    });
    attachment->sendInitialUpdate();
}

void ChoiceButtons::resized()
{
    auto r = getLocalBounds();
    if (segmented) r = r.reduced (3);
    const int n = buttons.size();
    const int gap = segmented ? 2 : 4;
    const int w = (r.getWidth() - gap * (n - 1)) / juce::jmax (1, n);
    for (int i = 0; i < n; ++i)
        buttons[i]->setBounds (r.getX() + i * (w + gap), r.getY(), i == n - 1 ? r.getRight() - (r.getX() + i * (w + gap)) : w, r.getHeight());
}

void ChoiceButtons::paint (juce::Graphics& g)
{
    if (! segmented) return;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (field);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (fieldBd);
    g.drawRoundedRectangle (r, 9.0f, 1.0f);
}

//==============================================================================
void SizeButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (on ? accent : panel.brighter (down ? 0.12f : (over ? 0.06f : 0.0f)));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (on ? accent : juce::Colour (0xff2e3036));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    auto t = r.reduced (12.0f, 0.0f);
    g.setColour (on ? ink : theme::text);
    g.setFont (mono (17.0f));
    g.drawText (juce::String (n), t, juce::Justification::centredLeft, false);
    g.setColour ((on ? ink : theme::text).withAlpha (0.75f));
    g.setFont (ui (11.0f));
    g.drawText (u ("choristes"), t, juce::Justification::centredRight, false);
}

SizeColumn::SizeColumn (APVTS& state)
{
    auto* param = state.getParameter (ids::size);
    for (int i = 0; i < choir::kNumSizes; ++i)
    {
        auto* b = buttons.add (new SizeButton (choir::kSizes[i]));
        b->setTooltip (u ("Chœur de ") + juce::String (choir::kSizes[i]) + u (" choristes"));
        b->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) {
        const int idx = (int) std::lround (v);
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
        repaint();
    });
    attachment->sendInitialUpdate();
}

void SizeColumn::resized()
{
    const int n = buttons.size(), gap = 7;
    const float h = (float) (getHeight() - gap * (n - 1)) / (float) n;
    for (int i = 0; i < n; ++i)
        buttons[i]->setBounds (0, (int) std::lround (i * (h + gap)), getWidth(), (int) std::lround (h));
}

//==============================================================================
Knob::Knob (APVTS& state, const juce::String& paramId, const juce::String& lab, const juce::String& tip, bool bipolar)
    : param (state.getParameter (paramId)), label (lab)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (tip);
    slider.setMouseDragSensitivity (180);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().withHeight (48).withSizeKeepingCentre (48, 48));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withTrimmedTop (50.0f);
    auto val = param->getCurrentValueAsText();
    const auto unit = param->getLabel();
    if (unit.isNotEmpty() && val != u ("Coupée") && val != "Auto") val << " " << unit;
    g.setColour (text);
    g.setFont (mono (11.0f));
    g.drawText (val, r.removeFromTop (15.0f), juce::Justification::centred, false);
    g.setColour (muted);
    g.setFont (ui (11.0f));
    g.drawText (label, r.removeFromTop (15.0f), juce::Justification::centred, false);
}

//==============================================================================
BarParam::BarParam (APVTS& state, const juce::String& paramId, const juce::String& lab, juce::Colour colour, const juce::String& tip, bool bipolar)
    : param (state.getParameter (paramId)), label (lab)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::trackColourId, colour);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (tip);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
}

void BarParam::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (32).withHeight (16));
}

void BarParam::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (muted);
    g.setFont (ui (10.0f).withExtraKerningFactor (0.08f));
    g.drawText (label, r.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    auto val = param->getCurrentValueAsText();
    const auto unit = param->getLabel();
    if (unit.isNotEmpty() && val != "Auto") val << " " << unit;
    g.setColour (text);
    g.setFont (mono (12.0f));
    g.drawText (val, r.removeFromTop (16.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
SectionCard::SectionCard (APVTS& state, int s)
    : index (s), apvts (state),
      level (state, ids::sec (s, ids::level), u ("NIVEAU"), section (s), u ("Volume du pupitre")),
      spread (state, ids::sec (s, ids::spread), u ("LARGEUR"), section (s), u ("Place qu'occupe le pupitre sur la scène")),
      formant (state, ids::sec (s, ids::formant), u ("FORMANTS"), section (s),
               u ("Timbre : Auto = voix de femme ou d'homme selon le pupitre ; plus haut = plus clair, plus bas = plus sombre"), true),
      delay (state, ids::sec (s, ids::delay), u ("RETARD"), section (s), u ("Le pupitre chante un peu après la voix principale"))
{
    onButton.setColour (juce::ToggleButton::tickColourId, section (s));
    onButton.setTooltip (u ("Allumer / éteindre le pupitre"));
    onButton.onStateChange = [this] { repaint(); };
    addAndMakeVisible (onButton);
    onAttachment = std::make_unique<APVTS::ButtonAttachment> (state, ids::sec (s, ids::on), onButton);

    interval.addItemList (names::intervals(), 1);
    interval.setTooltip (u ("Intervalle chanté par ce pupitre (en mode MIDI, il suit les accords du clavier)"));
    addAndMakeVisible (interval);
    intervalAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ids::sec (s, ids::interval), interval);

    for (auto* b : { &level, &spread, &formant, &delay }) addAndMakeVisible (b);
}

void SectionCard::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    auto top = r.removeFromTop (26);
    onButton.setBounds (top.removeFromRight (34));
    top.removeFromRight (8);
    interval.setBounds (top.removeFromRight (156));
    r.removeFromTop (12);
    const int gap = 12;
    const int w = (r.getWidth() - 3 * gap) / 4;
    int x = r.getX();
    for (auto* b : { &level, &spread, &formant, &delay })
    {
        b->setBounds (x, r.getY(), w, 50);
        x += w + gap;
    }
}

void SectionCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto col = section (index);
    const bool on = onButton.getToggleState() && ! (index == 3 && apvts.getRawParameterValue (ids::voices)->load() < 0.5f);
    g.setColour (selected ? panelSel : panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (selected ? col : border);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);

    auto top = getLocalBounds().toFloat().reduced (14.0f, 12.0f).removeFromTop (26.0f);
    g.setColour (on ? col : col.withAlpha (0.3f));
    g.fillEllipse (top.getX(), top.getCentreY() - 6.0f, 12.0f, 12.0f);
    top.removeFromLeft (22.0f);
    g.setColour (on ? text : muted);
    g.setFont (ui (15.0f, true));
    const auto name = names::sectionNames()[index];
    g.drawText (name, top, juce::Justification::centredLeft, false);
    const float nw = juce::GlyphArrangement::getStringWidth (ui (15.0f, true), name) + 10.0f;
    static const char* ranges[4] = { "voix de femme · aiguë", "voix de femme · grave", "voix d'homme · aiguë", "voix d'homme · grave" };
    g.setColour (muted);
    g.setFont (ui (11.0f));
    g.drawText (on ? u (ranges[index]) : u ("éteint"), top.withTrimmedLeft (nw), juce::Justification::centredLeft, false);

    if (! on)
    {
        g.setColour (bg.withAlpha (0.45f));
        g.fillRoundedRectangle (r.withTrimmedTop (44.0f), 12.0f);
    }
}

void SectionCard::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (index);
}

//==============================================================================
ChoraleEditor::ChoraleEditor (ChoraleProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      modeButtons (p.apvts, ids::mode, names::modes(), true),
      sourceButtons (p.apvts, ids::source, names::sources(), true),
      voiceButtons (p.apvts, ids::voices, names::voiceCounts(), true),
      hallButtons (p.apvts, ids::hall, names::halls(), false),
      sizes (p.apvts),
      stageView (p.apvts)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    wordmark = juce::ImageCache::getFromMemory (BinaryData::logo_wordmark_png, BinaryData::logo_wordmark_pngSize);

    addAndMakeVisible (content);
    auto& apvts = p.apvts;

    // en-tête
    refreshPresetBox();
    presetBox.setTooltip (u ("Préréglages : choisir un type de chœur"));
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id > 0) proc.setCurrentProgram (id - 1);
    };
    content.addAndMakeVisible (presetBox);

    modeButtons.buttons[0]->setTooltip (u ("Diatonique : les harmonies restent dans la tonalité choisie (tierces majeures ou mineures selon la note)"));
    modeButtons.buttons[1]->setTooltip (u ("Intervalles fixes : toujours le même nombre de demi-tons"));
    modeButtons.buttons[2]->setTooltip (u ("MIDI : les pupitres chantent les notes des accords joués au clavier"));
    content.addAndMakeVisible (modeButtons);

    keyBox.addItemList (names::keys(), 1);
    keyBox.setTooltip (u ("Tonalité du morceau (mode diatonique). Auto : le plugin la déduit de ce que vous chantez"));
    content.addAndMakeVisible (keyBox);
    keyAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids::key, keyBox);
    scaleBox.addItemList (names::scales(), 1);
    scaleBox.setTooltip (u ("Mode de la gamme (mode diatonique)"));
    content.addAndMakeVisible (scaleBox);
    scaleAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids::scale, scaleBox);

    sourceButtons.buttons[0]->setTooltip (u ("La voix d'origine est une voix d'homme"));
    sourceButtons.buttons[1]->setTooltip (u ("La voix d'origine est une voix de femme"));
    content.addAndMakeVisible (sourceButtons);

    // colonnes
    content.addAndMakeVisible (sizes);
    stageView.onSelect = [this] (int s) { select (s); };
    content.addAndMakeVisible (stageView);

    voiceButtons.buttons[0]->setTooltip (u ("Trois pupitres (la basse se tait)"));
    voiceButtons.buttons[1]->setTooltip (u ("Quatre pupitres : soprano, alto, ténor, basse"));
    content.addAndMakeVisible (voiceButtons);
    foldButton.setColour (juce::ToggleButton::tickColourId, accent);
    foldButton.setTooltip (u ("Ramène chaque pupitre dans sa tessiture (par exemple : un homme peut faire chanter des sopranos)"));
    content.addAndMakeVisible (foldButton);
    foldAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, ids::fold, foldButton);

    for (int s = 0; s < choir::kSections; ++s)
    {
        auto* c = cards.add (new SectionCard (apvts, s));
        c->onSelect = [this] (int i) { select (i); };
        content.addAndMakeVisible (c);
    }

    // bas de la fenêtre
    realism.add (new Knob (apvts, ids::detune, u ("Désaccord"), u ("Écart de justesse entre les choristes")));
    realism.add (new Knob (apvts, ids::attack, u ("Attaques"), u ("Dispersion des attaques : les choristes ne commencent pas tous ensemble")));
    realism.add (new Knob (apvts, ids::vibrato, u ("Vibrato"), u ("Vibrato propre à chaque choriste (il s'installe au cours de la note)")));
    realism.add (new Knob (apvts, ids::breath, u ("Souffle"), u ("Air et respiration du chœur")));
    realism.add (new Knob (apvts, ids::humanity, u ("Humanité"), u ("Imperfections naturelles : dérive, timbre, niveau, glissés propres à chacun")));
    acoustics.add (new Knob (apvts, ids::distance, u ("Distance"), u ("Distance entre l'auditeur et le premier rang")));
    acoustics.add (new Knob (apvts, ids::reverb, u ("Réverb"), u ("Quantité d'acoustique du lieu")));
    acoustics.add (new Knob (apvts, ids::predelay, u ("Pré-délai"), u ("Temps avant les premières réflexions")));
    leadKnobs.add (new Knob (apvts, ids::lead, u ("Voix seule"), u ("Niveau de la voix d'origine (tout à gauche : coupée)")));
    leadKnobs.add (new Knob (apvts, ids::choirLevel, u ("Chœur"), u ("Niveau du chœur")));
    outKnobs.add (new Knob (apvts, ids::width, u ("Largeur"), u ("Largeur stéréo du chœur")));
    outKnobs.add (new Knob (apvts, ids::brightness, u ("Brillance"), u ("Air et présence dans les aigus du chœur")));
    outKnobs.add (new Knob (apvts, ids::gain, u ("Gain"), u ("Gain de sortie")));
    for (auto* arr : { &realism, &acoustics, &leadKnobs, &outKnobs })
        for (auto* k : *arr) content.addAndMakeVisible (k);

    content.addAndMakeVisible (hallButtons);
    hallButtons.buttons[0]->setTooltip (u ("Studio : acoustique courte et sèche"));
    hallButtons.buttons[1]->setTooltip (u ("Chapelle : petite et chaleureuse"));
    hallButtons.buttons[2]->setTooltip (u ("Église : grande nef"));
    hallButtons.buttons[3]->setTooltip (u ("Cathédrale : très longue réverbération"));

    qualityBox.addItemList (names::qualities(), 1);
    qualityBox.setTooltip (u ("Nombre de choristes réellement calculés par pupitre : Éco 8, Normale 16, Haute 32 (plus de détail, plus de calcul)"));
    content.addAndMakeVisible (qualityBox);
    qualityAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids::quality, qualityBox);

    midiButton.setButtonText (u ("MIDI → pupitres"));
    midiButton.setTooltip (u ("Choisir (ou glisser sur la fenêtre) la mélodie en MIDI — par exemple « Vocal To MIDI » d'ACE Studio : "
                              "le plugin écrit une piste par pupitre, harmonisée et avec les paroles, à réimporter dans ACE Studio"));
    midiButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (u ("Mélodie en MIDI (avec ses paroles)"), juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) arrangeMidi (fc.getResult()); });
    };
    content.addAndMakeVisible (midiButton);

    select (2);

    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 3 / 2, baseH * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / baseH);
    setSize (baseW, baseH);
    startTimerHz (20);
}

ChoraleEditor::~ChoraleEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void ChoraleEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < presets::count(); ++i) presetBox.addItem (presets::name (i), i + 1);
    presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);
}

void ChoraleEditor::select (int s)
{
    stageView.selected = s;
    stageView.repaint();
    for (auto* c : cards) { c->selected = c->index == s; c->repaint(); }
}

void ChoraleEditor::paint (juce::Graphics& g) { g.fillAll (bg); }

bool ChoraleEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi")) return true;
    return false;
}

void ChoraleEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi")) { arrangeMidi (juce::File (f)); return; }
}

void ChoraleEditor::arrangeMidi (const juce::File& f)
{
    const auto r = arranger::arrangeFile (f, proc.currentParams());
    auto opts = juce::MessageBoxOptions()
                    .withIconType (r.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon)
                    .withTitle (r.ok ? u ("Pupitres écrits") : u ("MIDI → pupitres"))
                    .withMessage (arranger::describe (r))
                    .withAssociatedComponent (this);
    if (r.ok)
    {
        const auto folder = r.files.getFirst();
        juce::AlertWindow::showAsync (opts.withButton (u ("Ouvrir le dossier")).withButton ("OK"),
                                      [folder] (int result) { if (result == 1) folder.revealToUser(); });
    }
    else juce::AlertWindow::showAsync (opts.withButton ("OK"), nullptr);
}

void ChoraleEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (juce::AffineTransform::scale (scale));
}

void ChoraleEditor::layoutContent()
{
    // tout est placé dans la taille de référence 1240 × 800 (la fenêtre entière est mise à l'échelle)
    headerArea = { 0, 0, (float) baseW, 64 };

    // en-tête : logo, préréglage, harmonie, tonalité, voix d'origine, note
    presetBox.setBounds (312, 15, 196, 34);
    modeButtons.setBounds (522, 15, 252, 34);
    keyBox.setBounds (786, 15, 70, 34);
    scaleBox.setBounds (862, 15, 146, 34);
    sourceButtons.setBounds (1018, 15, 124, 34);
    noteArea = { 1150, 15, 70, 34 };

    // colonnes
    sizeArea = { 20, 80, 140, 16 };
    sizes.setBounds (20, 104, 140, 510);
    stageCaption = { 176, 80, 600, 16 };
    stageView.setBounds (176, 104, 600, 510);

    sectionsArea = { 792, 80, 428, 16 };
    voiceButtons.setBounds (1220 - 112, 74, 112, 28);
    foldButton.setBounds (1220 - 112 - 130, 74, 124, 28);
    midiButton.setBounds (1220 - 112 - 130 - 112, 74, 106, 28);
    for (int s = 0; s < cards.size(); ++s) cards[s]->setBounds (792, 110 + s * 128, 428, 120);

    // bas : quatre groupes
    const float widths[4] = { 370, 370, 212, 212 };
    float x = 20;
    for (int i = 0; i < 4; ++i) { groupArea[i] = { x, 630, widths[i], 150 }; x += widths[i] + 12; }

    auto placeKnobs = [] (juce::OwnedArray<Knob>& ks, juce::Rectangle<float> area, float y, float kw, float reserveRight = 0)
    {
        const float total = kw * (float) ks.size();
        const float gap = ks.size() > 1 ? juce::jmin (14.0f, (area.getWidth() - 24 - reserveRight - total) / (float) (ks.size() - 1)) : 0;
        float kx = area.getX() + 12;
        for (auto* k : ks) { k->setBounds ((int) kx, (int) (area.getY() + y), (int) kw, 82); kx += kw + gap; }
    };
    placeKnobs (realism, groupArea[0], 40, 64);
    hallButtons.setBounds ((int) groupArea[1].getX() + 12, (int) groupArea[1].getY() + 34, (int) groupArea[1].getWidth() - 24, 26);
    placeKnobs (acoustics, groupArea[1], 66, 64);
    placeKnobs (leadKnobs, groupArea[2], 40, 64);
    placeKnobs (outKnobs, groupArea[3], 40, 54, 22);   // place du vumètre à droite
    qualityBox.setBounds ((int) groupArea[3].getRight() - 12 - 92, (int) groupArea[3].getY() + 8, 92, 24);
    meterArea = { groupArea[3].getRight() - 22, groupArea[3].getY() + 44, 8, 92 };
}

void ChoraleEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (bg);

    // en-tête
    g.setColour (header);
    g.fillRect (headerArea);
    g.setColour (border);
    g.drawHorizontalLine ((int) headerArea.getBottom() - 1, 0, (float) baseW);
    if (wordmark.isValid())
    {
        const float h = 30.0f, w = h * (float) wordmark.getWidth() / (float) wordmark.getHeight();
        g.setOpacity (0.95f);
        g.drawImage (wordmark, { 20.0f, 13.0f, w, h }, juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
        g.setColour (accent);
        g.setFont (title (23.0f));
        g.drawText ("CHORALE", juce::Rectangle<float> (20.0f + w + 12.0f, 14.0f, 140.0f, 36.0f), juce::Justification::centredLeft, false);
    }

    // note détectée
    g.setColour (field);
    g.fillRoundedRectangle (noteArea, 8.0f);
    g.setColour (fieldBd);
    g.drawRoundedRectangle (noteArea.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (muted);
    g.setFont (ui (9.0f).withExtraKerningFactor (0.1f));
    g.drawText ("NOTE", noteArea.withHeight (14.0f).translated (0, 3.0f), juce::Justification::centred);
    g.setColour (noteText == "-" ? dim : accent);
    g.setFont (mono (13.0f));
    g.drawText (noteText, noteArea.withTrimmedTop (14.0f), juce::Justification::centred);

    // tonalité détectée (mode Auto)
    if (keyText.isNotEmpty())
    {
        const auto r = scaleBox.getBounds().toFloat().reduced (0.5f);
        g.setColour (field);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (fieldBd);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        g.setColour (muted);
        g.setFont (ui (9.0f).withExtraKerningFactor (0.1f));
        g.drawText (u ("DÉTECTÉE"), r.withHeight (14.0f).translated (0, 2.0f), juce::Justification::centred);
        g.setColour (accent);
        g.setFont (ui (13.0f, true));
        g.drawText (keyText, r.withTrimmedTop (13.0f), juce::Justification::centred);
    }

    // titres des colonnes
    caption (g, u ("TAILLE DU CHŒUR"), sizeArea);
    caption (g, u ("SCÈNE — VUE DE DESSUS"), stageCaption);
    g.setColour (muted);
    g.setFont (ui (12.0f));
    g.drawText (lastStageInfo, stageCaption, juce::Justification::centredRight, false);
    caption (g, u ("PUPITRES"), sectionsArea);

    // groupes du bas
    const char* titles[4] = { "RÉALISME HUMAIN", "ACOUSTIQUE", "VOIX PRINCIPALE", "SORTIE" };
    for (int i = 0; i < 4; ++i)
    {
        const auto r = groupArea[i].reduced (0.5f);
        g.setColour (panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
        caption (g, u (titles[i]), groupArea[i].reduced (14.0f, 12.0f).withHeight (16.0f));
    }

    // vumètre de sortie
    g.setColour (border);
    g.fillRoundedRectangle (meterArea, 3.0f);
    const float db = meterLevel > 1e-5f ? 20.0f * std::log10 (meterLevel) : -100.0f;
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f);
    auto lit = meterArea.withTop (meterArea.getBottom() - meterArea.getHeight() * frac);
    g.setColour (db > -0.5f ? juce::Colour (0xffe07a5f) : accent);
    g.fillRoundedRectangle (lit, 3.0f);
}

void ChoraleEditor::timerCallback()
{
    // note chantée (notation française : do 3 = do central)
    const float midi = proc.getDetectedMidi();
    juce::String nt = "-";
    if (midi > 0)
    {
        const int n = (int) std::lround (midi);
        nt = names::noteNames()[((n % 12) + 12) % 12] + " " + juce::String (n / 12 - 2);
    }
    const bool silent = proc.getInputLevel() < 0.002f;
    if (silent) nt = "-";
    if (nt != noteText) { noteText = nt; content.repaint (noteArea.toNearestInt().expanded (2)); }

    const float lvl = proc.getOutputLevel();
    meterLevel = juce::jmax (lvl, meterLevel * 0.82f);
    content.repaint (meterArea.toNearestInt().expanded (2));

    const int size = choir::kSizes[juce::jlimit (0, choir::kNumSizes - 1, (int) proc.apvts.getRawParameterValue (ids::size)->load())];
    const juce::String info = juce::String (size) + u (" choristes (≈ ") + juce::String (stageView.singersPerSection())
                              + u (" par pupitre) · ") + juce::String (proc.getActiveVoices()) + u (" voix calculées");
    if (info != lastStageInfo) { lastStageInfo = info; content.repaint (stageCaption.toNearestInt().expanded (2)); }

    // tonalité automatique : la gamme détectée remplace le choix du mode
    const bool autoKey = (int) proc.apvts.getRawParameterValue (ids::key)->load() == choir::kKeyAuto;
    juce::String kt;
    if (autoKey)
    {
        const int k = proc.getDetectedKey();
        kt = k < 0 ? u ("à l'écoute…") : names::keys()[k % 12] + (k >= 12 ? u (" mineur") : u (" majeur"));
    }
    if (scaleBox.isVisible() == autoKey) scaleBox.setVisible (! autoKey);
    if (kt != keyText) { keyText = kt; content.repaint (scaleBox.getBounds().expanded (2)); }

    if (presetBox.getSelectedId() != proc.currentPreset + 1) presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);

    // la carte de la basse s'assombrit en mode 3 voix
    const int v = (int) proc.apvts.getRawParameterValue (ids::voices)->load();
    if (v != lastVoices) { lastVoices = v; for (auto* c : cards) c->repaint(); }
}
