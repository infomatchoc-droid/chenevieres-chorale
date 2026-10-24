// Tests du moteur (sans JUCE) : justesse de la détection, harmonies, intervalles produits, CPU, fichiers d'écoute.
//   EngineTest            → tests, doit finir par « TOUT EST OK »
//   EngineTest wav DIR    → écrit aussi des fichiers WAV de démonstration dans DIR
//   EngineTest in.wav out.wav [taille] [lieu]  → traite un fichier (mono ou stéréo, 16/24/32 bits)
#include "../Source/ChoirEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace choir;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  ÉCHEC : "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

//------------------------------------------------------------------------------
// WAV
static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const int n = (int) l.size(), ch = 2, bps = 16;
    const int dataBytes = n * ch * 2;
    auto w32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (ch); w32 (sr); w32 (sr * ch * 2); w16 (ch * 2); w16 (bps);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const float x = std::max (-1.0f, std::min (1.0f, (c == 0 ? l : r)[(size_t) i]));
            const int16_t s = (int16_t) std::lround (x * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}

static bool readWav (const std::string& path, std::vector<float>& l, std::vector<float>& r, int& sr)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    size_t k;
    while ((k = std::fread (buf, 1, sizeof buf, f)) > 0) b.insert (b.end(), buf, buf + k);
    std::fclose (f);
    if (b.size() < 44 || std::memcmp (b.data(), "RIFF", 4) != 0) return false;
    int ch = 0, bits = 0, fmt = 0;
    size_t p = 12;
    while (p + 8 <= b.size())
    {
        const uint32_t sz = (uint32_t) b[p + 4] | ((uint32_t) b[p + 5] << 8) | ((uint32_t) b[p + 6] << 16) | ((uint32_t) b[p + 7] << 24);
        if (std::memcmp (&b[p], "fmt ", 4) == 0)
        {
            fmt = b[p + 8] | (b[p + 9] << 8);
            ch = b[p + 10] | (b[p + 11] << 8);
            sr = (int) ((uint32_t) b[p + 12] | ((uint32_t) b[p + 13] << 8) | ((uint32_t) b[p + 14] << 16) | ((uint32_t) b[p + 15] << 24));
            bits = b[p + 22] | (b[p + 23] << 8);
            if (fmt == 0xFFFE) fmt = b[p + 32] | (b[p + 33] << 8);
        }
        else if (std::memcmp (&b[p], "data", 4) == 0)
        {
            const int bytes = bits / 8;
            const size_t frames = std::min ((size_t) sz, b.size() - p - 8) / (size_t) (bytes * ch);
            l.resize (frames); r.resize (frames);
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < std::min (ch, 2); ++c)
                {
                    const unsigned char* q = &b[p + 8 + (i * (size_t) ch + (size_t) c) * (size_t) bytes];
                    float x = 0;
                    if (fmt == 3 && bits == 32) std::memcpy (&x, q, 4);
                    else if (bits == 16) x = (float) (int16_t) (q[0] | (q[1] << 8)) / 32768.0f;
                    else if (bits == 24) { int v = q[0] | (q[1] << 8) | (q[2] << 16); if (v & 0x800000) v |= ~0xFFFFFF; x = (float) v / 8388608.0f; }
                    else if (bits == 32) { int32_t v; std::memcpy (&v, q, 4); x = (float) v / 2147483648.0f; }
                    (c == 0 ? l : r)[i] = x;
                }
            if (ch == 1) r = l;
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

//------------------------------------------------------------------------------
// Voix chantée de synthèse : impulsions glottiques (Rosenberg) + formants de la voyelle « a » + vibrato
struct Note { float midi; float dur; };

static std::vector<float> synthVoice (int sr, const std::vector<Note>& notes, bool female, std::vector<float>* truthMidi)
{
    std::vector<float> out;
    const float F[4] = { female ? 850.0f : 730.0f, female ? 1220.0f : 1090.0f, female ? 2810.0f : 2440.0f, female ? 3800.0f : 3400.0f };
    const float BW[4] = { 90, 110, 160, 220 };
    double a1[4], a2[4], st1[4] = {}, st2[4] = {};
    for (int k = 0; k < 4; ++k)
    {
        const double r = std::exp (-3.14159265 * BW[k] / sr);
        a1[k] = -2 * r * std::cos (2 * 3.14159265 * F[k] / sr);
        a2[k] = r * r;
    }
    double phase = 0, vib = 0;
    uint32_t seed = 1234;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f * 2 - 1; };
    float prevMidi = notes.front().midi;
    for (const Note& nt : notes)
    {
        const int len = (int) (nt.dur * sr);
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) i / sr;
            const float glide = std::min (1.0f, t / 0.06f);
            float midi = prevMidi + (nt.midi - prevMidi) * glide;
            const float vibEnv = std::min (1.0f, std::max (0.0f, (t - 0.25f) / 0.4f));
            vib += 2 * 3.14159265 * 5.3 / sr;
            midi += 0.35f * vibEnv * (float) std::sin (vib);
            const double f0 = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            if (truthMidi) truthMidi->push_back (midi);
            phase += f0 / sr;
            if (phase >= 1) phase -= 1;
            // Rosenberg : ouverture 0–0,4, fermeture 0,4–0,6
            double g;
            if (phase < 0.4) g = 0.5 * (1 - std::cos (3.14159265 * phase / 0.4));
            else if (phase < 0.6) g = std::cos (3.14159265 * (phase - 0.4) / 0.4);
            else g = 0;
            static double prevG = 0;
            double x = (g - prevG) * 4.0 + 0.012 * rnd();   // dérivée du débit + souffle
            prevG = g;
            double y = 0;
            for (int k = 0; k < 4; ++k)
            {
                const double v = x - a1[k] * st1[k] - a2[k] * st2[k];
                st2[k] = st1[k]; st1[k] = v;
                y += v * (k == 0 ? 1.0 : (k == 1 ? 0.6 : 0.25));
            }
            // enveloppe de note (attaque et fin douces)
            const float env = std::min (1.0f, t / 0.03f) * std::min (1.0f, (nt.dur - t) / 0.04f);
            out.push_back ((float) (y * 0.02 * env));
        }
        prevMidi = nt.midi;
    }
    // normalisation à -6 dBFS
    float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
    for (float& v : out) v *= 0.5f / pk;
    return out;
}

// Hauteur moyenne (YIN simple, Hz) d'un extrait
static double measurePitch (const float* x, int n, int sr, double fmin = 60, double fmax = 1200)
{
    const int tmax = (int) (sr / fmin), tmin = (int) (sr / fmax);
    const int W = n - tmax - 1;
    if (W < 64) return 0;
    std::vector<double> d ((size_t) tmax + 1);
    double run = 0;
    d[0] = 1;
    for (int tau = 1; tau <= tmax; ++tau)
    {
        double s = 0;
        for (int j = 0; j < W; ++j) { const double df = x[j] - x[j + tau]; s += df * df; }
        run += s;
        d[(size_t) tau] = run > 0 ? s * tau / run : 1;
    }
    int best = -1;
    for (int tau = tmin; tau < tmax; ++tau) if (d[(size_t) tau] < 0.15) { while (tau + 1 < tmax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau; best = tau; break; }
    if (best < 0) { double m = 1e9; for (int tau = tmin; tau < tmax; ++tau) if (d[(size_t) tau] < m) { m = d[(size_t) tau]; best = tau; } }
    const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
    const double off = 0.5 * (a - c) / (a - 2 * b + c);
    return sr / (best + off);
}

static void run (Engine& e, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    L.assign (in.size(), 0.0f); R.assign (in.size(), 0.0f);
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        const float* ins[1] = { in.data() + pos };
        float* outs[2] = { L.data() + pos, R.data() + pos };
        e.process (ins, 1, outs, 2, n);
    }
}

static Params dryTestParams()
{
    Params p;
    p.detuneCents = 0; p.vibrato = 0; p.humanity = 0; p.breath = 0; p.attackMs = 0;
    p.reverb = 0; p.leadDb = -60; p.width = 1; p.distanceM = 4;
    p.mode = HarmonyMode::Fixed;
    p.sizeIndex = 0;  // 3 choristes
    for (int s = 0; s < kSections; ++s) { p.sec[(size_t) s].on = false; p.sec[(size_t) s].delayMs = 0; p.sec[(size_t) s].formant = 0; }
    return p;
}

//------------------------------------------------------------------------------
int main (int argc, char** argv)
{
    const int sr = 48000;

    if (argc >= 3 && std::string (argv[1]) != "wav")
    {
        std::vector<float> l, r; int fsr = 0;
        if (! readWav (argv[1], l, r, fsr)) { std::printf ("lecture impossible : %s\n", argv[1]); return 1; }
        Engine e; e.prepare (fsr, 512);
        // réglages : nom=valeur (size=0-8 hall=0-3 key=0-11 scale=0-4 female=0/1 mode=0-2 quality=0-2 voices=3/4 fold=0/1
        //            dist rev attack vib breath hum det lead choir width pre ; s0int=… s0on= s0lvl= s0x= s0y= s0form= s0delay= s0spread=)
        Params p;
        for (int a = 3; a < argc; ++a)
        {
            const std::string arg = argv[a];
            const auto eq = arg.find ('=');
            if (eq == std::string::npos) continue;
            const std::string k = arg.substr (0, eq);
            const float v = (float) std::atof (arg.c_str() + eq + 1);
            if (k == "size") p.sizeIndex = (int) v; else if (k == "hall") p.hall = (int) v; else if (k == "key") p.keyRoot = (int) v;
            else if (k == "scale") p.scale = (int) v; else if (k == "female") p.sourceFemale = v > 0.5f; else if (k == "mode") p.mode = (HarmonyMode) (int) v;
            else if (k == "quality") p.quality = (int) v; else if (k == "voices") p.numSections = (int) v; else if (k == "fold") p.fold = v > 0.5f;
            else if (k == "dist") p.distanceM = v; else if (k == "rev") p.reverb = v / 100; else if (k == "attack") p.attackMs = v;
            else if (k == "vib") p.vibrato = v / 100; else if (k == "breath") p.breath = v / 100; else if (k == "hum") p.humanity = v / 100;
            else if (k == "det") p.detuneCents = v; else if (k == "lead") p.leadDb = v; else if (k == "choir") p.choirDb = v;
            else if (k == "width") p.width = v / 100; else if (k == "pre") p.predelayMs = v;
            else if (k.size() > 2 && k[0] == 's' && k[1] >= '0' && k[1] <= '3')
            {
                auto& s = p.sec[(size_t) (k[1] - '0')];
                const std::string w = k.substr (2);
                if (w == "int") s.interval = (int) v; else if (w == "on") s.on = v > 0.5f; else if (w == "lvl") s.levelDb = v;
                else if (w == "x") s.x = v; else if (w == "y") s.y = v; else if (w == "form") s.formant = v / 100;
                else if (w == "delay") s.delayMs = v; else if (w == "spread") s.spread = v / 100;
            }
            else std::printf ("réglage inconnu : %s\n", k.c_str());
        }
        e.setParams (p);
        const int lat = e.getLatencySamples();
        std::vector<float> in (l.size() + (size_t) lat + (size_t) fsr * 4, 0.0f);
        for (size_t i = 0; i < l.size(); ++i) in[i] = 0.5f * (l[i] + r[i]);
        std::vector<float> L, R;
        run (e, in, L, R, 512);
        L.erase (L.begin(), L.begin() + lat); R.erase (R.begin(), R.begin() + lat);
        float pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        // normalisé à -1 dBFS crête (comparaison d'écoute)
        if (pk > 0) for (size_t i = 0; i < L.size(); ++i) { L[i] *= 0.89f / pk; R[i] *= 0.89f / pk; }
        writeWav (argv[2], L, R, fsr);
        std::printf ("écrit : %s (crête %.2f, latence %d)\n", argv[2], pk, lat);
        return 0;
    }

    //--- 1. harmonie diatonique
    std::printf ("1. Harmonie diatonique (ré mineur)\n");
    {
        int prev = -1000;
        // ré mineur : ré mi fa sol la si♭ do
        CHECK (diatonicOffset (62, 2, 1, 2, &prev) == 3, "ré + tierce = fa (+3)");
        prev = -1000; CHECK (diatonicOffset (65, 2, 1, 2, &prev) == 4, "fa + tierce = la (+4)");
        prev = -1000; CHECK (diatonicOffset (69, 2, 1, 2, &prev) == 3, "la + tierce = do (+3)");
        prev = -1000; CHECK (diatonicOffset (70, 2, 1, -2, &prev) == -3, "si♭ - tierce = sol (-3)");
        prev = -1000; CHECK (diatonicOffset (64, 2, 1, 4, &prev) == 6, "mi + quinte = si♭ (+6, quinte diminuée)");
        prev = -1000; CHECK (diatonicOffset (62, 2, 1, -7, &prev) == -12, "octave au-dessous = -12");
        prev = -1000; CHECK (diatonicOffset (62.3f, 2, 1, 2, &prev) == 3, "ré un peu haut reste ré");
        // hystérésis : une voix qui monte doucement de ré vers mi ne change de référence qu'après 0,75 demi-ton
        prev = -1000; diatonicOffset (62, 2, 1, 2, &prev);
        CHECK (diatonicOffset (62.7f, 2, 1, 2, &prev) == 3, "hystérésis (ré gardé à +0,7)");
        CHECK (diatonicOffset (63.9f, 2, 1, 2, &prev) == 3, "mi + tierce = sol (+3)");
        // do majeur
        prev = -1000; CHECK (diatonicOffset (64, 0, 0, 2, &prev) == 3, "do majeur : mi + tierce = sol (+3)");
        prev = -1000; CHECK (diatonicOffset (60, 0, 0, 5, &prev) == 9, "do majeur : do + sixte = la (+9)");
    }

    //--- 2. détection de hauteur
    std::printf ("2. Détection de hauteur\n");
    std::vector<float> truth;
    const std::vector<Note> melodyMale = { { 50, 0.9f }, { 52, 0.6f }, { 53, 0.6f }, { 55, 0.6f }, { 57, 1.2f }, { 53, 0.6f }, { 50, 1.2f } };
    const std::vector<float> voiceMale = synthVoice (sr, melodyMale, false, &truth);
    {
        Engine e; e.prepare (sr, 256);
        Params p = dryTestParams(); p.sec[0].on = true;
        e.setParams (p);
        std::vector<float> L, R;
        // mesure note par note pendant le traitement
        double errSum = 0; int errN = 0, octaveErr = 0;
        L.assign (voiceMale.size(), 0); R.assign (voiceMale.size(), 0);
        const int lat = e.getLatencySamples();
        for (size_t pos = 0; pos < voiceMale.size(); pos += 256)
        {
            const int n = (int) std::min ((size_t) 256, voiceMale.size() - pos);
            const float* ins[1] = { voiceMale.data() + pos };
            float* outs[2] = { L.data() + pos, R.data() + pos };
            e.process (ins, 1, outs, 2, n);
            const float det = e.getDetectedMidi();
            // la note détectée correspond à l'entrée d'il y a ~ la latence (marques en retard)
            const long tIdx = (long) pos + n - lat;
            if (det > 0 && tIdx > 0)
            {
                const float tr = truth[(size_t) tIdx];
                const float err = std::abs (det - tr);
                if (err > 6) ++octaveErr;
                else { errSum += err; ++errN; }
            }
        }
        const double meanErr = errN ? errSum / errN : 99;
        std::printf ("   erreur moyenne %.3f demi-ton sur %d mesures, %d sauts d'octave, latence %d (%.1f ms)\n", meanErr, errN, octaveErr, lat, lat * 1000.0 / sr);
        CHECK (meanErr < 0.35, "erreur de détection trop grande (%.3f)", meanErr);
        CHECK (octaveErr == 0, "%d sauts d'octave", octaveErr);
        CHECK (lat < 0.07 * sr, "latence trop grande");
    }

    //--- 3. intervalles produits (un seul pupitre, choristes identiques)
    std::printf ("3. Intervalles produits\n");
    {
        const std::vector<Note> held = { { 55, 2.5f } };   // sol 2 tenu, sans vibrato au début
        std::vector<float> tr;
        std::vector<float> v = synthVoice (sr, held, false, &tr);
        const struct { int interval; float semis; const char* name; } cases[] = {
            { 0, 0, "unisson" }, { 4, 7, "quinte ↑" }, { 7, 12, "octave ↑" }, { 9, -4, "tierce ↓" }, { 14, -12, "octave ↓" }, { 2, 4, "tierce ↑" } };
        for (const auto& c : cases)
        {
            Engine e; e.prepare (sr, 256);
            Params p = dryTestParams(); p.fold = false;
            p.sec[2].on = true; p.sec[2].interval = c.interval;   // ténor (formants d'homme, comme la source)
            e.setParams (p);
            std::vector<float> L, R;
            run (e, v, L, R);
            const int lat = e.getLatencySamples();
            const int start = (int) (0.25 * sr) + lat, len = (int) (0.12 * sr);   // avant le vibrato de la source
            const double fin = measurePitch (v.data() + (int) (0.25 * sr), len, sr);
            const double fout = measurePitch (L.data() + start, len, sr);
            const double semis = 12 * std::log2 (fout / fin);
            std::printf ("   %-9s attendu %+5.1f  mesuré %+6.2f demi-tons\n", c.name, c.semis, semis);
            CHECK (std::abs (semis - c.semis) < 0.25, "%s : %.2f au lieu de %.1f", c.name, semis, c.semis);
        }
    }

    //--- 4. tessiture et MIDI
    std::printf ("4. Tessiture automatique et accords MIDI\n");
    {
        const std::vector<Note> held = { { 50, 2.0f } };   // ré 2 (voix d'homme grave)
        std::vector<float> v = synthVoice (sr, held, false, nullptr);
        auto measureSection = [&] (Params p, bool midi, int expectSemis, const char* what)
        {
            Engine e; e.prepare (sr, 256);
            e.setParams (p);
            if (midi) { e.noteOn (57); e.noteOn (62); e.noteOn (65); }   // la, ré, fa
            std::vector<float> L, R;
            run (e, v, L, R);
            const int lat = e.getLatencySamples();
            const double fout = measurePitch (L.data() + (int) (0.3 * sr) + lat, (int) (0.12 * sr), sr);
            const double fin = measurePitch (v.data() + (int) (0.3 * sr), (int) (0.12 * sr), sr);
            const double semis = 12 * std::log2 (fout / fin);
            std::printf ("   %-40s attendu %+3d  mesuré %+6.2f\n", what, expectSemis, semis);
            CHECK (std::abs (semis - expectSemis) < 0.3, "%s : %.2f", what, semis);
        };
        Params p = dryTestParams(); p.mode = HarmonyMode::Diatonic; p.keyRoot = 2; p.scale = 1;
        p.sec[0].on = true; p.sec[0].interval = 0;                     // soprano à l'unisson d'un ré 2 → ramenée dans sa tessiture (+12)
        measureSection (p, false, 12, "soprano, unisson d'une voix grave");
        p = dryTestParams(); p.mode = HarmonyMode::Midi;
        p.sec[0].on = true;                                            // seule : prend la note la plus haute (fa 3 = 65), près de son centre
        measureSection (p, true, 15, "MIDI : soprano seule → fa 3");
        p = dryTestParams(); p.mode = HarmonyMode::Midi;
        p.sec[3].on = true;                                            // basse seule → fa près de 50 → fa 3 (53)
        measureSection (p, true, 3, "MIDI : basse seule → fa 2");
    }

    //--- 5. chœurs complets : stabilité, niveau, CPU
    std::printf ("5. Chœur complet (toutes tailles) : stabilité, niveau, CPU\n");
    {
        for (int sz = 0; sz < kNumSizes; ++sz)
        {
            for (int q = 0; q < 3; q += 2)
            {
                if (q == 2 && sz < 6) continue;
                Engine e; e.prepare (sr, 512);
                Params p; p.sizeIndex = sz; p.quality = q;
                e.setParams (p);
                std::vector<float> L, R;
                const auto t0 = std::chrono::steady_clock::now();
                run (e, voiceMale, L, R, 512);
                const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                const double audio = (double) voiceMale.size() / sr;
                bool finite = true; double pw = 0; float pk = 0;
                for (size_t i = 0; i < L.size(); ++i)
                {
                    if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false;
                    pw += L[i] * L[i] + R[i] * R[i]; pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
                }
                const double rmsDb = 10 * std::log10 (pw / (2.0 * L.size()) + 1e-20);
                std::printf ("   %4d choristes, qualité %d : %3d calculés, CPU %5.1f %% d'un cœur, niveau %6.1f dB RMS, crête %.2f\n",
                             kSizes[sz], q, e.getActiveVoices(), 100 * secs / audio, rmsDb, pk);
                CHECK (finite, "valeurs non finies (%d choristes)", kSizes[sz]);
                CHECK (rmsDb > -40 && rmsDb < -3, "niveau anormal (%d choristes) : %.1f dB", kSizes[sz], rmsDb);
                CHECK (pk < 2.5f, "crête trop forte (%d choristes)", kSizes[sz]);
            }
        }
    }

    //--- 6. changements de réglages en cours de lecture (pas de plantage, pas de NaN)
    std::printf ("6. Changements de réglages en cours de lecture\n");
    {
        Engine e; e.prepare (sr, 512);
        Params p; e.setParams (p);
        std::vector<float> L (512), R (512);
        bool finite = true;
        uint32_t seed = 7;
        auto rnd = [&seed] (int m) { seed = seed * 1664525u + 1013904223u; return (int) ((seed >> 8) % (uint32_t) m); };
        for (size_t pos = 0; pos + 512 <= voiceMale.size(); pos += 512)
        {
            if (rnd (4) == 0)
            {
                p.sizeIndex = rnd (kNumSizes); p.hall = rnd (kNumHalls); p.mode = (HarmonyMode) rnd (3);
                p.quality = rnd (3); p.numSections = 3 + rnd (2); p.attackMs = (float) rnd (150);
                for (auto& s : p.sec) { s.interval = rnd (kNumIntervals); s.x = rnd (100) / 100.0f; s.y = rnd (100) / 100.0f; s.delayMs = (float) rnd (100); }
                e.setParams (p);
                if (rnd (3) == 0) e.noteOn (50 + rnd (20));
            }
            const float* ins[1] = { voiceMale.data() + pos };
            float* outs[2] = { L.data(), R.data() };
            e.process (ins, 1, outs, 2, 512);
            for (int i = 0; i < 512; ++i) if (! std::isfinite (L[(size_t) i]) || std::abs (L[(size_t) i]) > 10) finite = false;
        }
        CHECK (finite, "valeurs anormales après changements de réglages");
        // silence après la fin : la queue de réverbération s'éteint
        std::vector<float> zeros ((size_t) sr * 12, 0.0f), ZL, ZR;
        run (e, zeros, ZL, ZR, 512);
        float tail = 0; for (size_t i = ZL.size() - 4800; i < ZL.size(); ++i) tail = std::max (tail, std::abs (ZL[i]));
        CHECK (tail < 1e-3f, "la réverbération ne s'éteint pas (%.5f)", tail);
        // fréquences d'échantillonnage
        for (int fs : { 44100, 88200, 96000, 192000 })
        {
            Engine e2; e2.prepare (fs, 512); e2.setParams (Params());
            std::vector<float> v2 = synthVoice (fs, { { 57, 1.0f } }, false, nullptr), l2, r2;
            run (e2, v2, l2, r2, 512);
            bool ok = true; for (float x : l2) if (! std::isfinite (x)) ok = false;
            CHECK (ok, "valeurs non finies à %d Hz", fs);
        }
    }

    //--- fichiers d'écoute
    if (argc >= 3 && std::string (argv[1]) == "wav")
    {
        const std::string dir = argv[2];
        std::vector<float> v = voiceMale;
        v.resize (v.size() + (size_t) sr * 4, 0.0f);
        writeWav (dir + "/00-voix-seule.wav", v, v, sr);
        for (int sz : { 1, 4, 5, 8 })
        {
            Engine e; e.prepare (sr, 512);
            Params p; p.sizeIndex = sz;
            e.setParams (p);
            std::vector<float> L, R;
            run (e, v, L, R, 512);
            const int lat = e.getLatencySamples();
            L.erase (L.begin(), L.begin() + lat); R.erase (R.begin(), R.begin() + lat);
            char name[256]; std::snprintf (name, sizeof name, "%s/%02d-choeur-%d.wav", dir.c_str(), sz, kSizes[sz]);
            writeWav (name, L, R, sr);
        }
        std::printf ("fichiers écrits dans %s\n", dir.c_str());
    }

    std::printf (failures == 0 ? "\nTOUT EST OK\n" : "\n%d ÉCHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
