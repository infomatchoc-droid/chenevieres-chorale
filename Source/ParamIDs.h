// Identifiants des paramètres et libellés (partagés par le processeur, l'interface et les préréglages)
#pragma once

#include <juce_core/juce_core.h>
#include "ChoirEngine.h"

namespace ids
{
inline const char* const size     = "size";
inline const char* const mode     = "mode";
inline const char* const key      = "key";
inline const char* const scale    = "scale";
inline const char* const source   = "source";
inline const char* const voices   = "voices";
inline const char* const quality  = "quality";
inline const char* const fold     = "fold";
inline const char* const detune   = "detune";
inline const char* const attack   = "attack";
inline const char* const vibrato  = "vibrato";
inline const char* const breath   = "breath";
inline const char* const humanity = "humanity";
inline const char* const hall     = "hall";
inline const char* const distance = "distance";
inline const char* const reverb   = "reverb";
inline const char* const predelay = "predelay";
inline const char* const lead     = "lead";
inline const char* const choirLevel = "choir";
inline const char* const width    = "width";
inline const char* const gain     = "gain";
inline const char* const brightness = "bright";

// Paramètres de pupitre : « s0_on », « s2_int »…
inline juce::String sec (int s, const char* what) { return "s" + juce::String (s) + "_" + what; }
inline const char* const on = "on";
inline const char* const interval = "int";
inline const char* const level = "lvl";
inline const char* const spread = "spread";
inline const char* const formant = "form";
inline const char* const delay = "delay";
inline const char* const x = "x";
inline const char* const y = "y";
}

namespace names
{
inline juce::String u (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

inline juce::StringArray sizes()
{
    juce::StringArray a;
    for (int i = 0; i < choir::kNumSizes; ++i) a.add (juce::String (choir::kSizes[i]));
    return a;
}
inline juce::StringArray modes() { return { u ("Diatonique"), u ("Intervalles"), u ("MIDI") }; }
inline juce::StringArray keys() { return { u ("Do"), u ("Do♯"), u ("Ré"), u ("Mi♭"), u ("Mi"), u ("Fa"), u ("Fa♯"), u ("Sol"), u ("La♭"), u ("La"), u ("Si♭"), u ("Si"), u ("Auto") }; }
inline juce::StringArray scales() { return { u ("majeur"), u ("mineur"), u ("mineur harmonique"), u ("dorien"), u ("mixolydien") }; }
inline juce::StringArray sources() { return { u ("Homme"), u ("Femme") }; }
inline juce::StringArray voiceCounts() { return { u ("3 voix"), u ("4 voix") }; }
inline juce::StringArray qualities() { return { u ("Éco"), u ("Normale"), u ("Haute") }; }
inline juce::StringArray halls() { return { u ("Studio"), u ("Chapelle"), u ("Église"), u ("Cathédrale") }; }
inline juce::StringArray intervals()
{
    return { u ("Unisson"), u ("Seconde ↑"), u ("Tierce ↑"), u ("Quarte ↑"), u ("Quinte ↑"), u ("Sixte ↑"), u ("Septième ↑"), u ("Octave ↑"),
             u ("Seconde ↓"), u ("Tierce ↓"), u ("Quarte ↓"), u ("Quinte ↓"), u ("Sixte ↓"), u ("Septième ↓"), u ("Octave ↓"),
             u ("Dixième ↓"), u ("Octave + quinte ↓"), u ("Deux octaves ↓") };
}
inline juce::StringArray sectionNames() { return { u ("Soprano"), u ("Alto"), u ("Ténor"), u ("Basse") }; }
inline juce::StringArray sectionShort() { return { "S", "A", "T", "B" }; }
inline juce::StringArray noteNames() { return { u ("Do"), u ("Do♯"), u ("Ré"), u ("Mi♭"), u ("Mi"), u ("Fa"), u ("Fa♯"), u ("Sol"), u ("La♭"), u ("La"), u ("Si♭"), u ("Si") }; }
}
