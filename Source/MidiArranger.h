// MIDI → pupitres : à partir de la mélodie chantée (par exemple « Vocal To MIDI » d'ACE Studio, paroles comprises),
// écrit une piste MIDI par pupitre, harmonisée comme dans le plugin (tonalité, intervalle, tessiture), en gardant
// les paroles à leur place. Les pistes se réimportent dans ACE Studio pour être chantées par de vraies voix d'IA.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "ChoirEngine.h"

namespace arranger
{
struct Report
{
    bool ok = false;
    juce::String error;
    int keyRoot = -1;            // tonique utilisée (0 = do)
    bool minor = false;
    bool keyDetected = false;    // déduite de la mélodie (tonalité « Auto »)
    int melodyNotes = 0;
    int lyricEvents = 0;
    juce::StringArray sectionNames;     // pupitres écrits
    juce::Array<int> octaveShift;       // octave choisie pour chaque pupitre écrit (en demi-tons)
    juce::Array<juce::File> files;      // fichiers écrits
};

// Harmonise un fichier MIDI en mémoire : piste 0 = tempo / mesure, puis une piste par pupitre allumé
juce::MidiFile arrange (const juce::MidiFile& in, const choir::Params& p, Report& report);

// Lit `midiIn`, écrit à côté « nom - Pupitres.mid » (toutes les pistes) et « nom - Soprano.mid »… (une par pupitre)
Report arrangeFile (const juce::File& midiIn, const choir::Params& p);

// Texte français résumant le résultat (pour la fenêtre du plugin)
juce::String describe (const Report& r);
}
