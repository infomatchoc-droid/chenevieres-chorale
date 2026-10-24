#include "MidiArranger.h"
#include "ParamIDs.h"

#include <algorithm>
#include <cmath>

namespace arranger
{
namespace
{
struct Note { double on, off; int pitch; juce::uint8 vel; };

const char* const kTrackNames[choir::kSections] = { "Soprano", "Alto", "Tenor", "Basse" };

bool isLyricOrText (const juce::MidiMessage& m)
{
    return m.isMetaEvent() && (m.getMetaEventType() == 0x05 || m.getMetaEventType() == 0x01);
}

// Tonalité d'après la durée de chaque classe de note (profils de Krumhansl-Kessler), comme le moteur
int detectKey (const std::vector<Note>& notes, bool& minor)
{
    float h[12] {};
    for (const auto& n : notes) h[((n.pitch % 12) + 12) % 12] += (float) std::max (1.0, n.off - n.on);
    static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float mnr[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    int best = 0; float bestC = -2;
    for (int k = 0; k < 24; ++k)
    {
        const float* prof = k < 12 ? maj : mnr;
        const int root = k % 12;
        float mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += h[i]; my += prof[(i - root + 12) % 12]; }
        mx /= 12; my /= 12;
        float sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float a = h[i] - mx, b = prof[(i - root + 12) % 12] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        const float c = sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0f;
        if (c > bestC) { bestC = c; best = k; }
    }
    minor = best >= 12;
    return best % 12;
}

// Octave du pupitre pour tout le morceau : le plus petit déplacement qui met la hauteur médiane bien dans
// la tessiture (même règle que la tessiture automatique du moteur)
int chooseOctave (float median, const choir::Register& reg)
{
    if (median >= reg.lo + 3.0f && median <= reg.hi - 3.0f) return 0;
    for (int step = 1; step <= 3; ++step)
        for (int k : { step, -step })
        {
            const float t = median + 12.0f * (float) k;
            if (t >= reg.lo + 3.0f && t <= reg.hi - 3.0f) return 12 * k;
        }
    int bestK = 0; float bestD = 1e9f;
    for (int k = -3; k <= 3; ++k)
    {
        const float d = std::abs (median + 12.0f * (float) k - reg.centre);
        if (d < bestD) { bestD = d; bestK = k; }
    }
    return 12 * bestK;
}
} // namespace

juce::MidiFile arrange (const juce::MidiFile& in, const choir::Params& p, Report& report)
{
    report = Report();
    juce::MidiFile out;
    const short tf = in.getTimeFormat();
    if (tf > 0) out.setTicksPerQuarterNote (tf);
    else out.setSmpteTimeFormat (-(tf >> 8), tf & 0xff);

    // la mélodie : la piste qui a le plus de notes
    int melodyTrack = -1, mostNotes = 0;
    for (int t = 0; t < in.getNumTracks(); ++t)
    {
        int n = 0;
        for (const auto* e : *in.getTrack (t)) if (e->message.isNoteOn()) ++n;
        if (n > mostNotes) { mostNotes = n; melodyTrack = t; }
    }
    if (melodyTrack < 0) { report.error = juce::String (juce::CharPointer_UTF8 ("Ce fichier MIDI ne contient aucune note.")); return out; }

    juce::MidiMessageSequence mel (*in.getTrack (melodyTrack));
    mel.updateMatchedPairs();
    std::vector<Note> notes;
    juce::MidiMessageSequence lyrics;
    for (const auto* e : mel)
    {
        if (e->message.isNoteOn())
        {
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : e->message.getTimeStamp() + 1;
            notes.push_back ({ e->message.getTimeStamp(), off, e->message.getNoteNumber(), e->message.getVelocity() });
        }
        else if (isLyricOrText (e->message)) lyrics.addEvent (e->message);
    }
    report.melodyNotes = (int) notes.size();
    report.lyricEvents = lyrics.getNumEvents();

    // piste 0 : tempo, mesure, armure… de toutes les pistes (sans notes ni paroles)
    juce::MidiMessageSequence conductor;
    for (int t = 0; t < in.getNumTracks(); ++t)
        for (const auto* e : *in.getTrack (t))
        {
            const auto& m = e->message;
            if (m.isTempoMetaEvent() || m.isTimeSignatureMetaEvent() || m.isKeySignatureMetaEvent())
                conductor.addEvent (m);
        }
    conductor.updateMatchedPairs();
    out.addTrack (conductor);

    // tonalité
    int root = p.keyRoot, scale = p.scale;
    if (root == choir::kKeyAuto)
    {
        bool minor = false;
        root = detectKey (notes, minor);
        scale = minor ? 1 : 0;
        report.keyDetected = true;
    }
    report.keyRoot = root;
    report.minor = scale == 1 || scale == 2;

    // médiane de la mélodie (pondérée par la durée)
    std::vector<std::pair<int, double>> pd;
    for (const auto& n : notes) pd.push_back ({ n.pitch, n.off - n.on });

    for (int s = 0; s < choir::kSections; ++s)
    {
        const auto& sp = p.sec[(size_t) s];
        if (! sp.on || (s == 3 && p.numSections < 4)) continue;
        const auto& iv = choir::kIntervals[std::clamp (sp.interval, 0, choir::kNumIntervals - 1)];
        const auto& reg = choir::kRegisters[s];

        // intervalle de chaque note : de la gamme (diatonique, et aussi en mode MIDI), ou fixe
        std::vector<int> off (notes.size());
        for (size_t i = 0; i < notes.size(); ++i)
            off[i] = p.mode == choir::HarmonyMode::Fixed
                         ? iv.semis
                         : (int) std::lround (choir::diatonicOffset ((float) notes[i].pitch, root, scale, iv.steps, nullptr));

        // octave du pupitre, une fois pour tout le morceau
        int shift = 0;
        if (p.fold)
        {
            std::vector<std::pair<float, double>> v;
            double total = 0;
            for (size_t i = 0; i < notes.size(); ++i) { v.push_back ({ (float) (notes[i].pitch + off[i]), notes[i].off - notes[i].on }); total += notes[i].off - notes[i].on; }
            std::sort (v.begin(), v.end());
            double acc = 0; float median = v.empty() ? 60.0f : v.back().first;
            for (const auto& x : v) { acc += x.second; if (acc >= total * 0.5) { median = x.first; break; } }
            shift = chooseOctave (median, reg);
        }

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, kTrackNames[s]), 0);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            int pitch = notes[i].pitch + off[i] + shift;
            if (p.fold)   // filet de sécurité pour une note très loin du reste
            {
                while (pitch > (int) reg.hi + 4) pitch -= 12;
                while (pitch < (int) reg.lo - 4) pitch += 12;
            }
            pitch = juce::jlimit (0, 127, pitch);
            seq.addEvent (juce::MidiMessage::noteOn (1, pitch, notes[i].vel), notes[i].on);
            seq.addEvent (juce::MidiMessage::noteOff (1, pitch), notes[i].off);
        }
        for (const auto* e : lyrics) seq.addEvent (e->message);
        seq.sort();
        seq.updateMatchedPairs();
        out.addTrack (seq);
        report.sectionNames.add (kTrackNames[s]);
        report.octaveShift.add (shift);
    }
    if (report.sectionNames.isEmpty()) { report.error = juce::String (juce::CharPointer_UTF8 ("Aucun pupitre n'est allumé.")); return out; }
    report.ok = true;
    return out;
}

Report arrangeFile (const juce::File& midiIn, const choir::Params& p)
{
    Report r;
    juce::MidiFile in;
    {
        juce::FileInputStream is (midiIn);
        if (! is.openedOk() || ! in.readFrom (is))
        {
            r.error = juce::String (juce::CharPointer_UTF8 ("Impossible de lire ce fichier MIDI."));
            return r;
        }
    }
    auto out = arrange (in, p, r);
    if (! r.ok) return r;

    const auto dir = midiIn.getParentDirectory();
    const auto base = midiIn.getFileNameWithoutExtension();
    auto write = [&r] (const juce::MidiFile& f, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream os (file);
        if (os.openedOk() && f.writeTo (os, 1)) { r.files.add (file); return true; }
        r.ok = false;
        r.error = juce::String (juce::CharPointer_UTF8 ("Impossible d'écrire ")) + file.getFullPathName();
        return false;
    };
    if (! write (out, dir.getChildFile (base + " - Pupitres.mid"))) return r;
    for (int i = 0; i < r.sectionNames.size(); ++i)
    {
        juce::MidiFile one;
        const short tf = in.getTimeFormat();
        if (tf > 0) one.setTicksPerQuarterNote (tf); else one.setSmpteTimeFormat (-(tf >> 8), tf & 0xff);
        one.addTrack (*out.getTrack (0));
        one.addTrack (*out.getTrack (i + 1));
        if (! write (one, dir.getChildFile (base + " - " + r.sectionNames[i] + ".mid"))) return r;
    }
    return r;
}

juce::String describe (const Report& r)
{
    using names::u;
    if (! r.ok) return r.error;
    juce::String s;
    s << u ("Tonalité ") << (r.keyDetected ? u ("détectée : ") : u ("choisie : "))
      << names::keys()[r.keyRoot] << (r.minor ? u (" mineur") : u (" majeur")) << "\n";
    s << r.melodyNotes << u (" notes") << (r.lyricEvents > 0 ? u (", ") + juce::String (r.lyricEvents) + u (" syllabes de paroles conservées") : u (", pas de paroles dans le fichier")) << "\n\n";
    for (int i = 0; i < r.sectionNames.size(); ++i)
    {
        s << u ("• ") << r.sectionNames[i];
        if (r.octaveShift[i] != 0) s << u (" (") << (r.octaveShift[i] > 0 ? "+" : "") << juce::String (r.octaveShift[i] / 12) << u (" octave pour la tessiture)");
        s << "\n";
    }
    s << "\n" << u ("Fichiers écrits à côté de l'original :") << "\n";
    for (const auto& f : r.files) s << f.getFileName() << "\n";
    return s;
}
} // namespace arranger
