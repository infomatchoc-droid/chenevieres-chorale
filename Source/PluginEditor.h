// Chènevières Chorale — interface (d'après la maquette validée)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "StageView.h"
#include "Theme.h"

using APVTS = juce::AudioProcessorValueTreeState;

// Rangée de boutons liée à un paramètre à choix (mode d'harmonie, lieu, voix d'origine…)
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (APVTS& state, const juce::String& paramId, const juce::StringArray& labels, bool segmented);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::OwnedArray<juce::TextButton> buttons;

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool segmented;
};

// Bouton de taille du chœur (« 100 choristes »)
class SizeButton : public juce::Button
{
public:
    explicit SizeButton (int singers) : juce::Button (juce::String (singers)), n (singers) { setClickingTogglesState (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    int n;
};

class SizeColumn : public juce::Component
{
public:
    explicit SizeColumn (APVTS& state);
    void resized() override;
private:
    juce::OwnedArray<SizeButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Bouton rotatif avec sa valeur et son nom
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& label, const juce::String& tip, bool bipolar = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::RangedAudioParameter* param;
    juce::String label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Petite glissière horizontale des cartes de pupitre
class BarParam : public juce::Component
{
public:
    BarParam (APVTS& state, const juce::String& paramId, const juce::String& label, juce::Colour colour, const juce::String& tip, bool bipolar = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::RangedAudioParameter* param;
    juce::String label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

class SectionCard : public juce::Component
{
public:
    SectionCard (APVTS& state, int section);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool selected = false;
    std::function<void (int)> onSelect;
    int index;

private:
    APVTS& apvts;
    juce::ToggleButton onButton;
    juce::ComboBox interval;
    std::unique_ptr<APVTS::ButtonAttachment> onAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> intervalAttachment;
    BarParam level, spread, formant, delay;
};

class ChoraleEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChoraleEditor (ChoraleProcessor&);
    ~ChoraleEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1240, baseH = 800;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (ChoraleEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void resized() override { owner.layoutContent(); }
        ChoraleEditor& owner;
    };

    void paintContent (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    void select (int s);
    void refreshPresetBox();

    ChoraleProcessor& proc;
    theme::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    Content content { *this };

    juce::Image wordmark;
    juce::ComboBox presetBox, keyBox, scaleBox, qualityBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> keyAtt, scaleAtt, qualityAtt;
    ChoiceButtons modeButtons, sourceButtons, voiceButtons, hallButtons;
    juce::ToggleButton foldButton { juce::String (juce::CharPointer_UTF8 ("Tessiture auto")) };
    std::unique_ptr<APVTS::ButtonAttachment> foldAtt;

    SizeColumn sizes;
    StageView stageView;
    juce::OwnedArray<SectionCard> cards;
    juce::OwnedArray<Knob> realism, acoustics, leadKnobs, outKnobs;

    // zones dessinées
    juce::Rectangle<float> headerArea, sizeArea, stageCaption, sectionsArea, noteArea, meterArea;
    juce::Rectangle<float> groupArea[4];
    juce::String noteText = "-";
    float meterLevel = 0;
    int lastVoices = -1;
    juce::String lastStageInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoraleEditor)
};
