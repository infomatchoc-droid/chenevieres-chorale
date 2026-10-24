// Préréglages d'usine
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace presets
{
int count();
juce::String name (int index);
// Remet tous les paramètres à leur valeur par défaut, puis applique le préréglage
void apply (juce::AudioProcessorValueTreeState& apvts, int index);
}
