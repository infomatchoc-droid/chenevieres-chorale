// Scène vue de dessus : chaque pupitre est un groupe de choristes qu'on déplace à la souris
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChoirEngine.h"

class StageView : public juce::Component, private juce::Timer
{
public:
    explicit StageView (juce::AudioProcessorValueTreeState& state);
    ~StageView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    int selected = 2;
    std::function<void (int)> onSelect;

    // pour l'en-tête du panneau
    int singersPerSection() const;
    float stageWidthMetres() const;

private:
    void timerCallback() override;
    juce::Rectangle<float> floorArea() const;
    juce::Point<float> centreOf (int s) const;
    float radiusPx (int s) const;
    int hitSection (juce::Point<float> p) const;
    bool sectionVisible (int s) const;
    float value (const juce::String& id) const;
    juce::int64 stateHash() const;

    juce::AudioProcessorValueTreeState& apvts;
    int dragging = -1, hover = -1;
    juce::Point<float> dragOffset;
    juce::int64 lastHash = 0;
};
