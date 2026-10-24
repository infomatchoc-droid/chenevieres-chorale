// Test du processeur JUCE : dispositions d'entrées/sorties, préréglages, sauvegarde, MIDI, latence
#include "../Source/PluginProcessor.h"
#include "../Source/ParamIDs.h"
#include "../Source/Presets.h"
#include "../Source/MidiArranger.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  ÉCHEC : "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static bool runAudio (ChoraleProcessor& p, int inCh, int outCh, int blocks, bool midi = false)
{
    juce::AudioBuffer<float> b (juce::jmax (inCh, outCh), 480);
    juce::MidiBuffer m;
    double ph = 0;
    bool finite = true;
    for (int k = 0; k < blocks; ++k)
    {
        b.clear();
        for (int c = 0; c < inCh; ++c)
            for (int i = 0; i < 480; ++i)
                b.setSample (c, i, (float) (0.3 * std::sin (2 * juce::MathConstants<double>::pi * (ph + i * 146.83 / 48000.0))));
        ph += 480 * 146.83 / 48000.0;
        m.clear();
        if (midi && k == 3) { m.addEvent (juce::MidiMessage::noteOn (1, 62, 0.8f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 65, 0.8f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 69, 0.8f), 10); }
        if (midi && k == 60) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (b, m);
        for (int c = 0; c < outCh; ++c)
            for (int i = 0; i < 480; ++i)
                if (! std::isfinite (b.getSample (c, i)) || std::abs (b.getSample (c, i)) > 8.0f) finite = false;
    }
    return finite;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<ChoraleProcessor> p (static_cast<ChoraleProcessor*> (createPluginFilter()));

    std::printf ("1. Dispositions d'entrées / sorties\n");
    {
        using CS = juce::AudioChannelSet;
        auto layout = [] (CS in, CS out) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return l; };
        CHECK (p->checkBusesLayoutSupported (layout (CS::mono(), CS::stereo())), "mono → stéréo");
        CHECK (p->checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "stéréo → stéréo");
        CHECK (p->checkBusesLayoutSupported (layout (CS::mono(), CS::mono())), "mono → mono");
        CHECK (! p->checkBusesLayoutSupported (layout (CS::stereo(), CS::mono())), "stéréo → mono refusé");
        CHECK (! p->checkBusesLayoutSupported (layout (CS::create5point1(), CS::create5point1())), "5.1 refusé");

        for (auto [in, out] : { std::pair { 1, 2 }, std::pair { 2, 2 }, std::pair { 1, 1 } })
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (in == 1 ? CS::mono() : CS::stereo());
            l.outputBuses.add (out == 1 ? CS::mono() : CS::stereo());
            p->setBusesLayout (l);
            p->prepareToPlay (48000, 480);
            CHECK (runAudio (*p, in, out, 120), "valeurs anormales (%d → %d)", in, out);
        }
        CHECK (p->getLatencySamples() > 0 && p->getLatencySamples() < 48000 / 10, "latence annoncée : %d", p->getLatencySamples());
        std::printf ("   latence annoncée : %d échantillons (%.1f ms)\n", p->getLatencySamples(), p->getLatencySamples() / 48.0);
    }

    std::printf ("2. Préréglages\n");
    {
        CHECK (presets::count() >= 10, "trop peu de préréglages");
        for (int i = 0; i < presets::count(); ++i)
        {
            p->setCurrentProgram (i);
            CHECK (p->getCurrentProgram() == i, "programme %d", i);
            CHECK (runAudio (*p, 2, 2, 60), "préréglage %s", presets::name (i).toRawUTF8());
        }
        p->setCurrentProgram (7);   // Gospel : 3 voix
        CHECK (p->apvts.getRawParameterValue (ids::voices)->load() < 0.5f, "Gospel en 3 voix");
        p->setCurrentProgram (2);
        CHECK (p->apvts.getRawParameterValue (ids::voices)->load() > 0.5f, "retour à 4 voix");
        CHECK ((int) p->apvts.getRawParameterValue (ids::size)->load() == 6, "grand chœur : 300 choristes");
        p->setCurrentProgram (0);
        bool allDefault = true;
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1e-4f) { allDefault = false; std::printf ("   %s\n", rp->getParameterID().toRawUTF8()); }
        CHECK (allDefault, "le premier préréglage doit être l'état par défaut");
    }

    std::printf ("3. Sauvegarde et rappel\n");
    {
        auto set = [&] (const juce::String& id, float v) { auto* par = p->apvts.getParameter (id); par->setValueNotifyingHost (par->convertTo0to1 (v)); };
        set (ids::size, 8); set (ids::hall, 1); set (ids::sec (2, ids::x), 0.123f); set (ids::sec (1, ids::interval), 4); set (ids::lead, -60);
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        set (ids::size, 0); set (ids::hall, 3); set (ids::sec (2, ids::x), 0.9f); set (ids::sec (1, ids::interval), 0); set (ids::lead, 0);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        auto v = [&] (const juce::String& id) { return p->apvts.getRawParameterValue (id)->load(); };
        CHECK ((int) v (ids::size) == 8, "taille rappelée");
        CHECK ((int) v (ids::hall) == 1, "lieu rappelé");
        CHECK (std::abs (v (ids::sec (2, ids::x)) - 0.123f) < 0.002f, "position rappelée");
        CHECK ((int) v (ids::sec (1, ids::interval)) == 4, "intervalle rappelé");
        CHECK (v (ids::lead) <= -59.9f, "voix principale coupée rappelée");
        CHECK (p->apvts.getParameter (ids::lead)->getCurrentValueAsText() == juce::String (juce::CharPointer_UTF8 ("Coupée")), "texte « Coupée »");
    }

    std::printf ("4. MIDI\n");
    {
        auto* par = p->apvts.getParameter (ids::mode);
        par->setValueNotifyingHost (par->convertTo0to1 (2.0f));
        CHECK (runAudio (*p, 2, 2, 120, true), "mode MIDI");
    }

    std::printf ("5. Paramètres : noms et nombre\n");
    {
        const int n = p->getParameters().size();
        std::printf ("   %d paramètres\n", n);
        CHECK (n == 22 + 8 * choir::kSections, "nombre de paramètres : %d", n);
        juce::StringArray seen;
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                CHECK (! seen.contains (rp->getParameterID()), "identifiant en double : %s", rp->getParameterID().toRawUTF8());
                seen.add (rp->getParameterID());
                CHECK (rp->getName (64).isNotEmpty(), "paramètre sans nom");
            }
    }

    std::printf ("6. MIDI → pupitres (paroles conservées)\n");
    {
        // mélodie en la majeur avec paroles, comme un export « Vocal To MIDI »
        juce::MidiFile in; in.setTicksPerQuarterNote (480);
        juce::MidiMessageSequence conductor, mel;
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent (600000), 0);
        conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
        const int pitches[] = { 57, 59, 61, 62, 64, 62, 61, 59, 57, 57, 64, 61 };
        const char* words[] = { "A", "ve", "Ma", "ri", "a", "gra", "ti", "a", "ple", "na", "Do", "mi" };
        for (int i = 0; i < 12; ++i)
        {
            const double t = i * 480.0;
            mel.addEvent (juce::MidiMessage::textMetaEvent (5, words[i]), t);
            mel.addEvent (juce::MidiMessage::noteOn (1, pitches[i], (juce::uint8) 100), t);
            mel.addEvent (juce::MidiMessage::noteOff (1, pitches[i]), t + 440);
        }
        mel.updateMatchedPairs();
        in.addTrack (conductor); in.addTrack (mel);

        choir::Params p;                         // tonalité Auto
        p.mode = choir::HarmonyMode::Diatonic;
        p.sec[0].interval = 2;                   // soprano : tierce au-dessus
        p.sec[1].interval = 0;                   // alto : unisson
        p.sec[2].interval = 14;                  // ténor : octave au-dessous
        p.sec[3].on = false;
        arranger::Report r;
        auto out = arranger::arrange (in, p, r);
        std::printf ("   %s\n", arranger::describe (r).replace ("\n", " | ").toRawUTF8());
        CHECK (r.ok, "échec : %s", r.error.toRawUTF8());
        CHECK (r.keyDetected && r.keyRoot == 9 && ! r.minor, "tonalité : %d %d", r.keyRoot, (int) r.minor);
        CHECK (out.getNumTracks() == 4, "pistes : %d", out.getNumTracks());
        auto notesOf = [] (const juce::MidiMessageSequence& sq) { juce::Array<int> a; for (auto* e : sq) if (e->message.isNoteOn()) a.add (e->message.getNoteNumber()); return a; };
        auto lyricsOf = [] (const juce::MidiMessageSequence& sq) { juce::StringArray a; for (auto* e : sq) if (e->message.isMetaEvent() && e->message.getMetaEventType() == 5) a.add (e->message.getTextFromTextMetaEvent()); return a; };
        const auto sop = notesOf (*out.getTrack (1)), alt = notesOf (*out.getTrack (2)), ten = notesOf (*out.getTrack (3));
        // tierces diatoniques en la majeur : la→do♯, si→ré, do♯→mi, ré→fa♯, mi→sol♯ (+ octave éventuelle pour la tessiture)
        const int thirds[] = { 61, 62, 64, 66, 68, 66, 64, 62, 61, 61, 68, 64 };
        bool okS = sop.size() == 12, okA = alt.size() == 12, okT = ten.size() == 12;
        for (int i = 0; i < 12 && okS; ++i) okS = ((sop[i] - thirds[i]) % 12 == 0);
        for (int i = 0; i < 12 && okA; ++i) okA = ((alt[i] - pitches[i]) % 12 == 0);
        for (int i = 0; i < 12 && okT; ++i) okT = (ten[i] == pitches[i] - 12);
        CHECK (okS, "soprano : tierces diatoniques");
        CHECK (okA, "alto : unisson (à l'octave près)");
        CHECK (okT, "ténor : octave au-dessous");
        int shiftS = sop.isEmpty() ? 0 : sop[0] - thirds[0];
        bool sameOct = true; for (int i = 0; i < sop.size(); ++i) if (sop[i] - thirds[i] != shiftS) sameOct = false;
        CHECK (sameOct, "soprano : une seule octave pour toute la ligne");
        for (int t = 1; t <= 3; ++t)
            CHECK (lyricsOf (*out.getTrack (t)).joinIntoString (" ") == "A ve Ma ri a gra ti a ple na Do mi", "paroles de la piste %d", t);
        bool tempo = false; for (auto* e : *out.getTrack (0)) if (e->message.isTempoMetaEvent()) tempo = true;
        CHECK (tempo, "tempo conservé");
        // écriture et relecture d'un fichier
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("melodie test.mid");
        { tmp.deleteFile(); juce::FileOutputStream os (tmp); in.writeTo (os, 1); }
        const auto rf = arranger::arrangeFile (tmp, p);
        CHECK (rf.ok && rf.files.size() == 4, "fichiers écrits : %d", rf.files.size());
        for (const auto& f : rf.files)
        {
            juce::MidiFile back; juce::FileInputStream is (f);
            CHECK (back.readFrom (is) && back.getNumTracks() >= 2, "relecture de %s", f.getFileName().toRawUTF8());
            f.deleteFile();
        }
        tmp.deleteFile();
    }

    std::printf (failures == 0 ? "\nTOUT EST OK\n" : "\n%d ÉCHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
