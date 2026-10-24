// Chènevières Chorale — moteur audio
#include "ChoirEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace choir
{
const int kSizes[kNumSizes] = { 3, 5, 10, 20, 50, 100, 300, 600, 1000 };

const IntervalDef kIntervals[kNumIntervals] = {
    { 0, 0 },                                        // Unisson
    { 1, 2 }, { 2, 4 }, { 3, 5 }, { 4, 7 }, { 5, 9 }, { 6, 10 }, { 7, 12 },        // seconde … octave au-dessus
    { -1, -2 }, { -2, -4 }, { -3, -5 }, { -4, -7 }, { -5, -9 }, { -6, -10 }, { -7, -12 }, // … au-dessous
    { -9, -16 }, { -11, -19 }, { -14, -24 }          // dixième, octave + quinte, deux octaves au-dessous
};

const Register kRegisters[kSections] = {
    { 59.0f, 84.0f, 71.0f },   // soprano  (si 3 – do 6)
    { 52.0f, 77.0f, 64.0f },   // alto     (mi 3 – fa 5)
    { 45.0f, 72.0f, 58.0f },   // ténor    (la 2 – do 5)
    { 36.0f, 66.0f, 50.0f }    // basse    (do 2 – fa♯ 4)
};

static const int kScalePcs[kNumScales][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },  // majeur
    { 0, 2, 3, 5, 7, 8, 10 },  // mineur naturel
    { 0, 2, 3, 5, 7, 8, 11 },  // mineur harmonique
    { 0, 2, 3, 5, 7, 9, 10 },  // dorien
    { 0, 2, 4, 5, 7, 9, 10 }   // mixolydien
};

Params::Params()
{
    // Disposition par défaut (maquette validée) : ténors et basses au fond, sopranos et altos devant
    sec[0] = { true, 0,  -2.0f, 0.60f, 0.0f, 6.0f, 0.20f, 0.68f };   // S : unisson
    sec[1] = { true, 9,  -4.0f, 0.55f, 0.0f, 8.0f, 0.80f, 0.68f };   // A : tierce au-dessous
    sec[2] = { true, 12, -5.0f, 0.70f, 0.0f, 11.0f, 0.30f, 0.30f };   // T : sixte au-dessous
    sec[3] = { true, 14, -6.0f, 0.65f, 0.0f, 13.0f, 0.70f, 0.30f };   // B : octave au-dessous
}

//==============================================================================
namespace
{
constexpr float kPi = 3.14159265358979f;
double kLowCutHz = 130.0, kPresenceHz = 700.0, kPresenceDb = 8.5, kAirHz = 7000.0;   // timbre du chœur
constexpr double kMinF0 = 75.0;     // plus grave détecté (latence = 4 périodes de cette note)
constexpr double kMaxF0 = 1100.0;

inline int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 1) : s (seed ? seed : 0x9e3779b9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }     // [0, 1)
    float bi() { return uni() * 2.0f - 1.0f; }                                 // [-1, 1)
    float gauss() { return (uni() + uni() + uni() + uni() - 2.0f) * 1.2247f; } // ~N(0, 1)
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setHighShelf (double sr, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * 3.141592653589793 * std::min (f, sr * 0.45) / sr;
        const double cw = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * al;
        const double B0 = A * ((A + 1) + (A - 1) * cw + sa), B1 = -2 * A * ((A - 1) + (A + 1) * cw), B2 = A * ((A + 1) + (A - 1) * cw - sa);
        const double A0 = (A + 1) - (A - 1) * cw + sa, A1 = 2 * ((A - 1) - (A + 1) * cw), A2 = (A + 1) - (A - 1) * cw - sa;
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void set (double sr, double f, double q, int type) // 0 passe-bas, 1 passe-haut, 2 passe-bande
    {
        f = std::min (f, sr * 0.45);
        const double w = 2.0 * 3.141592653589793 * f / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q);
        double B0, B1, B2;
        if (type == 0)      { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; }
        else if (type == 1) { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; }
        else                { B0 = al; B1 = 0; B2 = -al; }
        const double A0 = 1 + al;
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (-2 * cw / A0); a2 = (float) ((1 - al) / A0);
    }
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// Fenêtre de Hann tabulée sur [-1, 1]
struct HannTable
{
    static constexpr int N = 2048;
    float t[N + 2];
    HannTable() { for (int i = 0; i <= N + 1; ++i) { const float x = (float) i / N * 2.0f - 1.0f; t[i] = x > 1 ? 0.0f : 0.5f * (1.0f + std::cos (kPi * x)); } }
    inline float operator() (float x) const   // x dans [-1, 1]
    {
        const float p = (x + 1.0f) * (0.5f * N);
        if (p <= 0.0f || p >= (float) N) return 0.0f;
        const int i = (int) p;
        const float f = p - (float) i;
        return t[i] + f * (t[i + 1] - t[i]);
    }
};
const HannTable hann;

//==============================================================================
// Détection de hauteur : YIN sur un signal décimé à ~12 kHz
struct PitchTracker
{
    double sr = 48000, srD = 12000;
    int D = 4, tauMin = 10, tauMax = 160, hop = 32;
    Biquad aa1, aa2;
    std::vector<float> ring;   // signal décimé
    int ringMask = 0;
    int64_t decCount = 0;
    int phase = 0, sinceHop = 0;
    std::vector<float> d;
    bool voiced = false;
    float lastPeriod = 0;      // en échantillons pleine fréquence
    float hist[3] = { 0, 0, 0 };
    int histN = 0;
    int octaveHold = 0;

    struct Est { double t; float period; bool voiced; };
    static constexpr int kEst = 2048;
    Est est[kEst];
    int64_t estCount = 0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        D = std::max (1, (int) std::lround (sr / 12000.0));
        srD = sr / D;
        tauMin = std::max (2, (int) std::floor (srD / kMaxF0));
        tauMax = (int) std::ceil (srD / kMinF0);
        hop = std::max (8, (int) std::lround (srD * 0.0027));
        aa1.set (sr, srD * 0.38, 0.5412, 0);
        aa2.set (sr, srD * 0.38, 1.3066, 0);
        int sz = 1;
        while (sz < 4 * tauMax + 64) sz <<= 1;
        ring.assign ((size_t) sz, 0.0f);
        ringMask = sz - 1;
        d.assign ((size_t) tauMax + 2, 0.0f);
        reset();
    }
    void reset()
    {
        aa1.reset(); aa2.reset();
        std::fill (ring.begin(), ring.end(), 0.0f);
        decCount = 0; phase = 0; sinceHop = 0; voiced = false; lastPeriod = 0; histN = 0; octaveHold = 0;
        estCount = 0;
    }

    // Échantillon pleine fréquence d'indice absolu tFull ; renvoie vrai quand une estimation nouvelle est prête
    inline bool push (float x, int64_t tFull)
    {
        const float y = aa2.process (aa1.process (x));
        if (++phase < D) return false;
        phase = 0;
        ring[(size_t) (decCount & ringMask)] = y;
        ++decCount;
        if (++sinceHop < hop || decCount < 2 * tauMax + 2) return false;
        sinceHop = 0;
        analyse (tFull);
        return true;
    }

    void analyse (int64_t tFull)
    {
        const int W = tauMax;                       // fenêtre d'intégration
        const int64_t start = decCount - (W + tauMax);
        auto at = [this, start] (int i) { return ring[(size_t) ((start + i) & ringMask)]; };

        float energy = 0;
        for (int j = 0; j < W + tauMax; ++j) energy += at (j) * at (j);
        const float rms = std::sqrt (energy / (float) (W + tauMax));

        // différence cumulée normalisée
        d[0] = 1.0f;
        float running = 0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0;
            for (int j = 0; j < W; ++j) { const float df = at (j) - at (j + tau); s += df * df; }
            running += s;
            d[(size_t) tau] = running > 0 ? s * (float) tau / running : 1.0f;
        }

        int best = -1;
        const float thr = 0.13f;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[(size_t) tau] < thr)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        float cm = 1.0f;
        if (best < 0)
        {
            for (int tau = tauMin; tau < tauMax; ++tau) if (d[(size_t) tau] < cm) { cm = d[(size_t) tau]; best = tau; }
        }
        else cm = d[(size_t) best];

        // Erreur d'octave vers l'aigu (2e harmonique très fort, voyelles ouvertes) : on préfère le double
        // ou le triple de la période quand il est nettement meilleur, ou quand il prolonge la note en cours.
        // La continuité ne retient pas plus de ~40 ms (un vrai saut d'octave du chanteur finit par passer).
        if (best > 0)
        {
            const float prevD = voiced && lastPeriod > 0 ? lastPeriod / (float) D : 0.0f;
            bool held = false;
            for (int k = 3; k >= 2; --k)
            {
                const int c = best * k;
                if (c + 2 > tauMax) continue;
                const int lo = std::max (tauMin, (int) ((float) c * 0.92f)), hi = std::min (tauMax - 1, (int) ((float) c * 1.08f) + 1);
                int bk = lo;
                for (int t = lo; t <= hi; ++t) if (d[(size_t) t] < d[(size_t) bk]) bk = t;
                const float dk = d[(size_t) bk];
                const bool continuity = prevD > 0 && std::abs (std::log2 ((float) bk / prevD)) < 0.12f
                                        && std::abs (std::log2 ((float) best / prevD)) > 0.5f;
                const bool clearlyBetter = cm > 0.15f && dk < cm * 0.4f;   // premier creux médiocre, multiple très net
                if (clearlyBetter || (continuity && dk < cm + 0.06f && octaveHold < 16))
                {
                    held = ! clearlyBetter;
                    best = bk; cm = dk;
                    break;
                }
            }
            octaveHold = held ? octaveHold + 1 : 0;
        }

        float period = 0;
        if (best > tauMin && best < tauMax)
        {
            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float den = a - 2 * b + c;
            const float off = std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
            period = ((float) best + std::clamp (off, -0.5f, 0.5f)) * (float) D;
        }

        // la note est prise dès qu'elle est nette, et gardée jusqu'au bout de sa queue (plus bas, moins net)
        const bool loud = rms > (voiced ? 0.0016f : 0.0035f);
        const bool nowVoiced = loud && period > 0 && (voiced ? cm < 0.45f : cm < 0.2f);

        if (nowVoiced)
        {
            // médiane sur 3 estimations (évite les sauts d'octave isolés)
            hist[histN % 3] = period; ++histN;
            float p = period;
            if (histN >= 3)
            {
                float a = hist[0], b = hist[1], c = hist[2];
                p = std::max (std::min (a, b), std::min (std::max (a, b), c));
                // la valeur la plus récente si elle est proche de la médiane (suivi rapide des notes)
                if (std::abs (std::log2 (period / p)) < 0.08f) p = period;
            }
            lastPeriod = p;
        }
        else histN = 0;
        voiced = nowVoiced;

        // L'estimation vaut pour le centre de la fenêtre analysée
        const double centre = (double) tFull - (double) ((W + tauMax) / 2) * D;
        est[estCount % kEst] = { centre, voiced ? lastPeriod : 0.0f, voiced };
        ++estCount;
    }

    double knownUntil() const { return estCount > 0 ? est[(estCount - 1) % kEst].t : -1e18; }

    // Période (0 = non voisé) à l'instant t, par interpolation entre estimations
    float periodAt (double t, int64_t& cursor) const
    {
        if (estCount == 0) return 0;
        const int64_t oldest = std::max<int64_t> (0, estCount - kEst);
        if (cursor < oldest) cursor = oldest;
        while (cursor + 1 < estCount && est[(cursor + 1) % kEst].t <= t) ++cursor;
        const Est& a = est[cursor % kEst];
        if (cursor + 1 >= estCount || t <= a.t) return a.voiced ? a.period : 0.0f;
        const Est& b = est[(cursor + 1) % kEst];
        if (! a.voiced || ! b.voiced) return (t - a.t < b.t - t) ? (a.voiced ? a.period : 0.0f) : (b.voiced ? b.period : 0.0f);
        const float f = (float) ((t - a.t) / (b.t - a.t));
        return a.period + f * (b.period - a.period);
    }
};

//==============================================================================
struct Mark
{
    double pos;
    float period;
    float midi;
    bool voiced;
    float offset[kSections];   // intervalle de chaque pupitre (demi-tons), par rapport à la note de référence
    float dev[kSections];      // part de l'écart du chanteur (à sa note de référence) que les choristes ne suivent PAS
};

struct Voice
{
    // traits fixes du choriste
    float detune = 0, vibRate = 5.5f, vibDepth = 1, glideTau = 0.04f, attack = 0, formant = 1, amp = 1;
    float u = 0, v = 0;                 // place dans le pupitre (disque unité)
    float massPan = 0;
    Rng rng;

    // état
    bool active = false, fadingOut = false;
    double nextSynth = 0, lastHalfOut = 100, lastTs = 100;
    int64_t cursor = 0, cleared = 0;
    float offsetSm = 0, lastTarget = 1e9f, noteAge = 0;
    bool offsetInit = false;
    float drift = 0, driftTarget = 0, driftTimer = 0, vibPhase = 0;
    float tone = 1, ampRate = 0.3f, ampPhase = 0;   // couleur (aigus) et respiration du niveau
    // signature de phase propre au choriste (3 passe-tout) : des copies d'une même voix ne s'additionnent plus
    // en « peigne » (son métallique)
    float apc[3] {}, apxL[3] {}, apyL[3] {}, apxR[3] {}, apyR[3] {};
    float rampTime = 0.04f, phraseAge = 1.0f;       // entrée en fondu à chaque phrase
    float delayBase = 0, attackSpread = 0;          // retard sans l'attaque ; dispersion (redistribuée à chaque phrase)
    bool inGap = true;
    float vibWander = 0;

    // espace
    float delay = 0, delayTarget = 0;
    float gL = 0, gR = 0, gLt = 0, gRt = 0, mono = 0, monoT = 0;
    float itd = 0, itdSm = 0, airStateR = 0;
    float airCoef = 0, airState = 0;

    std::vector<float> acc;
    int accMask = 0;
};

struct MassTap
{
    float delayBase = 0, depth = 0, rate = 0, phase = 0, gL = 0, gR = 0, airCoef = 0, state = 0;
    float rate2 = 0.2f, phase2 = 0, ap = 0, apX = 0, apY = 0, rDelay = 0.5f;   // 2e modulation, passe-tout de décorrélation
};

struct Section
{
    std::array<Voice, kMaxVoicesPerSection> voices;
    int realCount = 0;          // choristes calculés
    int singers = 0;            // choristes représentés
    float realGain = 0, massGain = 0, massGainSm = 0;
    int prevDegree = -1000;     // hystérésis du mode diatonique
    float prevRef = -1.0f;      // hystérésis de la note de référence (demi-ton chanté)
    float devSlow = 0.0f;       // justesse lente du chanteur (glissés, notes un peu basses / hautes)
    int foldShift = 0;          // octave du pupitre (tessiture automatique)
    bool foldInit = false;
    int lastInterval = -1;

    static constexpr int kTaps = 8;
    std::array<MassTap, kTaps> taps;
    std::vector<float> massRing;
    int massMask = 0;
    int64_t massWrite = 0;

    // souffle
    Rng noise { 12345 };
    Biquad brL, brR, brHpL, brHpR;
    float breathPanL = 0.7f, breathPanR = 0.7f;
    float levelSm = 0;
};

//==============================================================================
// Réverbération : réseau de 16 retards (matrice de Hadamard), diffusion d'entrée, premières réflexions
struct Hall
{
    static constexpr int N = 16;
    double sr = 48000;
    std::vector<float> line[N];
    int len[N] {}, mask[N] {}, pos[N] {};
    float fa[N] {}, fb[N] {}, fs[N] {};
    float modPhase[2] { 0, 1.7f }, modRate[2] { 0.31f, 0.47f }, modDepth = 0;

    struct Allpass { std::vector<float> b; int len = 1, p = 0; float g = 0.6f;
        inline float process (float x) { const float z = b[(size_t) p]; const float y = -g * x + z; b[(size_t) p] = x + g * y; if (++p >= len) p = 0; return y; } };
    Allpass apL[4], apR[4];

    std::vector<float> pre;     // pré-délai et premières réflexions (mono)
    int preMask = 0;
    int64_t preW = 0;
    int preDelay = 0;
    int erT[12] {};
    float erG[12] {};
    Biquad inHp, inLp;

    float fadeGain = 1, fadeTarget = 1;
    int pendingHall = -1, currentHall = -1;

    static constexpr double baseMs[N] = { 31.7, 37.1, 41.3, 43.9, 47.9, 53.3, 59.1, 61.7, 67.3, 71.9, 73.1, 79.3, 83.9, 89.1, 97.3, 101.9 };
    struct Def { double size, rt60, hf, er, base; };
    static constexpr Def defs[kNumHalls] = {
        { 0.35, 0.55, 0.70, 0.30, 2.0 },    // Studio
        { 0.75, 1.70, 0.60, 0.65, 8.0 },    // Chapelle
        { 1.00, 3.40, 0.50, 1.00, 14.0 },   // Église
        { 1.55, 6.80, 0.42, 1.50, 22.0 }    // Cathédrale
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int i = 0; i < N; ++i)
        {
            const int maxLen = (int) (baseMs[i] * 1.6 * sr / 1000.0) + 64;
            int sz = 1; while (sz < maxLen) sz <<= 1;
            line[i].assign ((size_t) sz, 0.0f);
            mask[i] = sz - 1;
        }
        const double apMs[4] = { 1.31, 2.93, 4.37, 6.71 };
        for (int k = 0; k < 4; ++k)
        {
            apL[k].b.assign ((size_t) (apMs[k] * 2 * sr / 1000) + 8, 0.0f);
            apR[k].b.assign ((size_t) (apMs[k] * 2 * sr / 1000) + 8, 0.0f);
        }
        int sz = 1; while (sz < (int) (0.40 * sr)) sz <<= 1;   // pré-délai (120 ms) + réflexions (~220 ms)
        pre.assign ((size_t) sz, 0.0f); preMask = sz - 1;
        inHp.set (sr, 110, 0.7, 1);
        inLp.set (sr, 9000, 0.7, 0);
        currentHall = -1;
    }

    void configure (int h)
    {
        const Def& df = defs[h];
        for (int i = 0; i < N; ++i)
        {
            len[i] = std::max (8, (int) (baseMs[i] * df.size * sr / 1000.0));
            const double gdc = std::pow (10.0, -3.0 * len[i] / (df.rt60 * sr));
            const double gny = std::pow (10.0, -3.0 * len[i] / (df.rt60 * df.hf * sr));
            const double a = (gdc - gny) / (gdc + gny);
            fa[i] = (float) a; fb[i] = (float) (gdc * (1 - a)); fs[i] = 0;
            std::fill (line[i].begin(), line[i].end(), 0.0f);
            pos[i] = 0;
        }
        const double apMs[4] = { 1.31, 2.93, 4.37, 6.71 };
        for (int k = 0; k < 4; ++k)
        {
            apL[k].len = std::max (2, (int) (apMs[k] * (0.6 + 0.4 * df.size) * sr / 1000));
            apR[k].len = std::max (2, (int) (apMs[k] * 1.13 * (0.6 + 0.4 * df.size) * sr / 1000));
            std::fill (apL[k].b.begin(), apL[k].b.end(), 0.0f); apL[k].p = 0;
            std::fill (apR[k].b.begin(), apR[k].b.end(), 0.0f); apR[k].p = 0;
        }
        const double erMs[12] = { 7, 11, 17, 23, 29, 37, 43, 53, 61, 71, 79, 89 };
        for (int k = 0; k < 12; ++k)
        {
            erT[k] = (int) (erMs[k] * df.er * sr / 1000.0);
            erG[k] = (float) ((k % 2 ? -1 : 1) * 0.55 * std::pow (0.84, k));
        }
        modDepth = (float) (0.0004 * sr * df.size);
        currentHall = h;
    }

    void setHall (int h)
    {
        if (currentHall < 0) { configure (h); return; }
        if (h != currentHall && pendingHall != h) { pendingHall = h; fadeTarget = 0; }
    }

    void setPredelay (float ms) { preDelay = (int) (std::clamp (ms, 0.0f, 120.0f) * sr / 1000.0) + (currentHall >= 0 ? (int) (defs[currentHall].base * sr / 1000.0) : 0); }

    static inline void hadamard (float* x)
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { const float a = x[j], b = x[j + h]; x[j] = a + b; x[j + h] = a - b; }
        for (int i = 0; i < N; ++i) x[i] *= 0.25f;
    }

    inline void process (float inL, float inR, float& outL, float& outR)
    {
        // changement de lieu : fondu de sortie, nouvelle salle, fondu d'entrée
        fadeGain += (fadeTarget - fadeGain) * 0.0015f;
        if (pendingHall >= 0 && fadeGain < 0.002f) { configure (pendingHall); pendingHall = -1; fadeTarget = 1; }

        const float m = inLp.process (inHp.process (0.5f * (inL + inR)));
        pre[(size_t) (preW & preMask)] = m;
        const float pd = pre[(size_t) ((preW - preDelay) & preMask)];
        float erL = 0, erR = 0;
        for (int k = 0; k < 12; ++k)
        {
            const float s = pre[(size_t) ((preW - preDelay - erT[k]) & preMask)] * erG[k];
            if (k & 1) erR += s; else erL += s;
        }
        ++preW;

        const float dl = apL[3].process (apL[2].process (apL[1].process (apL[0].process (pd + 0.3f * (inL - inR) * 0.5f))));
        const float dr = apR[3].process (apR[2].process (apR[1].process (apR[0].process (pd - 0.3f * (inL - inR) * 0.5f))));

        float o[N], x[N];
        for (int i = 0; i < N; ++i)
        {
            float r;
            if (i < 2)
            {
                modPhase[i] += 2 * kPi * modRate[i] / (float) sr;
                if (modPhase[i] > 2 * kPi) modPhase[i] -= 2 * kPi;
                const float dd = (float) len[i] - 2 - modDepth * (1 + std::sin (modPhase[i]));
                const float rp = (float) pos[i] - dd;
                const int ip = (int) std::floor (rp);
                const float fr = rp - (float) ip;
                const float a = line[i][(size_t) (ip & mask[i])], b = line[i][(size_t) ((ip + 1) & mask[i])];
                r = a + fr * (b - a);
            }
            else r = line[i][(size_t) ((pos[i] - len[i]) & mask[i])];
            o[i] = r;
            fs[i] = fb[i] * r + fa[i] * fs[i];
            x[i] = fs[i];
        }
        hadamard (x);
        for (int i = 0; i < N; ++i)
        {
            line[i][(size_t) (pos[i] & mask[i])] = x[i] + ((i & 1) ? dr : dl) * 0.35f;
            pos[i] = (pos[i] + 1) & mask[i];
        }
        float l = 0, r = 0;
        for (int i = 0; i < N; ++i)
        {
            const float sgL = ((i * 5) & 4) ? -1.0f : 1.0f, sgR = ((i * 3) & 2) ? -1.0f : 1.0f;
            if (i & 1) r += o[i] * sgR; else l += o[i] * sgL;
        }
        outL = (l * 0.5f + erL) * fadeGain;
        outR = (r * 0.5f + erR) * fadeGain;
    }

    void clear()
    {
        for (int i = 0; i < N; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); fs[i] = 0; }
        for (int k = 0; k < 4; ++k) { std::fill (apL[k].b.begin(), apL[k].b.end(), 0.0f); std::fill (apR[k].b.begin(), apR[k].b.end(), 0.0f); }
        std::fill (pre.begin(), pre.end(), 0.0f);
        inHp.reset(); inLp.reset();
    }
};
constexpr double Hall::baseMs[Hall::N];
constexpr Hall::Def Hall::defs[kNumHalls];

} // namespace

//==============================================================================
float diatonicOffset (float midi, int keyRoot, int scale, int steps, int* prevDegree)
{
    const int* pcs = kScalePcs[std::clamp (scale, 0, kNumScales - 1)];
    auto noteOf = [pcs, keyRoot] (int degree) { const int o = floorDiv (degree, 7); return keyRoot + pcs[degree - o * 7] + 12 * o; };

    int deg = 0;
    if (prevDegree != nullptr && *prevDegree > -1000 && std::abs ((float) noteOf (*prevDegree) - midi) < 0.75f)
        deg = *prevDegree;           // on garde la note de référence tant que la voix n'en sort pas franchement
    else
    {
        const int o = floorDiv ((int) std::floor (midi) - keyRoot, 12);
        float bestDist = 1e9f;
        for (int k = o * 7 - 7; k <= o * 7 + 14; ++k)
        {
            const float dist = std::abs ((float) noteOf (k) - midi);
            if (dist < bestDist - 1e-4f) { bestDist = dist; deg = k; }
        }
    }
    if (prevDegree != nullptr) *prevDegree = deg;
    return (float) (noteOf (deg + steps) - noteOf (deg));
}

//==============================================================================
struct Engine::Impl
{
    double sr = 48000;
    int maxBlock = 512;
    int latency = 0;
    int itdMax = 0;
    double Tmax = 640, Tu = 288;

    // entrée
    std::vector<float> inMono, inLp;
    int inMask = 0;
    int64_t writePos = 0;
    float lp1 = 0, lp2 = 0, lpCoef = 0;
    std::vector<float> dryL, dryR;

    PitchTracker pitch;
    int64_t pitchCursor = 0;

    static constexpr int kMarkCap = 8192;
    std::vector<Mark> marks;
    int64_t markCount = 0;
    double lastMarkPos = 0;
    bool lastMarkVoiced = false;
    float avgMidi = -1.0f;
    bool noteStart = false;
    float keyHist[12] {}, keyHistTotal = 0, keyTimer = 0;
    int keyDetected = -1, keyPending = 0;
    std::atomic<int> keyOut { -1 };

    std::array<Section, kSections> sections;
    Hall hall;

    Params p, applied;
    bool firstParams = true;
    bool held[128] {};
    int chord[128] {};
    int chordSize = 0;
    bool chordDirty = false;

    // bus et état de sortie
    std::vector<float> busL, busR, mono;
    float envIn = 0, envCoefA = 0, envCoefR = 0;
    float leadSm = 0, choirSm = 0, outSm = 0, wetSm = 0, widthSm = 1;
    // Timbre du chœur (mesuré sur une référence du commerce, voix identique) : coupe-bas doux, présence, air
    Biquad hpL, hpR, presL, presR, shelfL, shelfR;
    float gainSmCoef = 0;

    std::atomic<float> detectedMidi { -1.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> inLevel { 0 }, outLevel { 0 };

    //--------------------------------------------------------------------------
    void prepare (double sampleRate, int block)
    {
        sr = sampleRate;
        maxBlock = std::max (1, block);
        Tmax = sr / kMinF0;
        Tu = std::max (16.0, 0.006 * sr);
        itdMax = (int) std::ceil (0.0007 * sr) + 2;

        pitch.prepare (sr);
        const int hopFull = pitch.hop * pitch.D;
        latency = (int) std::ceil (4.0 * Tmax) + 2 * hopFull + 16;

        // historique d'entrée : latence + retards (pupitre 100 ms, attaques 150 ms, scène ~150 ms) + marge
        int sz = 1; while (sz < latency + (int) (0.6 * sr) + 8 * (int) Tmax) sz <<= 1;
        inMono.assign ((size_t) sz, 0.0f);
        inLp.assign ((size_t) sz, 0.0f);
        dryL.assign ((size_t) sz, 0.0f);
        dryR.assign ((size_t) sz, 0.0f);
        inMask = sz - 1;
        lpCoef = (float) std::exp (-2.0 * 3.141592653589793 * 1100.0 / sr);

        marks.assign ((size_t) kMarkCap, Mark {});

        int accSz = 1; while (accSz < (int) (8 * Tmax) + maxBlock + 64) accSz <<= 1;
        int massSz = 1; while (massSz < (int) (0.25 * sr) + maxBlock) massSz <<= 1;
        for (int s = 0; s < kSections; ++s)
        {
            Section& sec = sections[(size_t) s];
            for (int i = 0; i < kMaxVoicesPerSection; ++i)
            {
                Voice& v = sec.voices[(size_t) i];
                v.acc.assign ((size_t) accSz, 0.0f);
                v.accMask = accSz - 1;
                // traits du choriste : tirés une fois pour toutes (le même chœur à chaque ouverture)
                Rng r ((uint32_t) (0x51ab + s * 977 + i * 7919));
                v.detune = r.gauss();
                v.vibRate = 4.7f + 1.5f * r.uni();
                v.vibDepth = 0.55f + 0.9f * r.uni();
                v.vibPhase = 2 * kPi * r.uni();
                v.glideTau = 0.018f + 0.07f * r.uni();
                v.attack = r.uni();
                v.formant = r.gauss();
                v.amp = r.gauss();
                v.tone = 0.6f + 0.8f * r.uni();
                for (int k = 0; k < 3; ++k) v.apc[k] = (0.25f + 0.5f * r.uni()) * (r.uni() < 0.5f ? -1.0f : 1.0f);
                v.rampTime = 0.02f + 0.04f * r.uni();
                v.ampRate = 0.12f + 0.45f * r.uni();
                v.ampPhase = 2 * kPi * r.uni();
                const float rad = std::sqrt (((float) i + 0.5f) / (float) kMaxVoicesPerSection);
                const float ang = (float) i * 2.39996f + (float) s;
                v.u = rad * std::cos (ang) + 0.08f * r.bi();
                v.v = rad * std::sin (ang) + 0.08f * r.bi();
                v.massPan = r.bi();
                v.rng = Rng ((uint32_t) (0xbeef + s * 131 + i * 31));
            }
            sec.massRing.assign ((size_t) massSz, 0.0f);
            sec.massMask = massSz - 1;
            Rng r ((uint32_t) (0x7a11 + s * 4099));
            for (auto& t : sec.taps)
            {
                t.rDelay = r.uni();
                t.delayBase = 0.016f + 0.055f * t.rDelay;
                t.depth = 0.0004f + 0.0008f * r.uni();
                t.rate = 0.05f + 0.10f * r.uni();
                t.rate2 = 0.13f + 0.19f * r.uni();
                t.phase2 = 2 * kPi * r.uni();
                t.ap = 0.75f * r.bi();
                t.phase = 2 * kPi * r.uni();
            }
            sec.brL.set (sr, 3800, 0.8, 2); sec.brR.set (sr, 4300, 0.8, 2);
            sec.brHpL.set (sr, 1500, 0.7, 1); sec.brHpR.set (sr, 1500, 0.7, 1);
            sec.noise = Rng ((uint32_t) (0x3141 + s * 59));
        }

        hall.prepare (sr);
        busL.assign ((size_t) maxBlock, 0.0f);
        busR.assign ((size_t) maxBlock, 0.0f);
        mono.assign ((size_t) maxBlock, 0.0f);
        envCoefA = (float) std::exp (-1.0 / (0.005 * sr));
        envCoefR = (float) std::exp (-1.0 / (0.12 * sr));
        gainSmCoef = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        firstParams = true;
        reset();
    }

    void reset()
    {
        std::fill (inMono.begin(), inMono.end(), 0.0f);
        std::fill (inLp.begin(), inLp.end(), 0.0f);
        std::fill (dryL.begin(), dryL.end(), 0.0f);
        std::fill (dryR.begin(), dryR.end(), 0.0f);
        writePos = 0; lp1 = lp2 = 0;
        pitch.reset(); pitchCursor = 0;
        markCount = 0; lastMarkPos = 0; lastMarkVoiced = false; avgMidi = -1.0f; noteStart = false;
        std::fill (std::begin (keyHist), std::end (keyHist), 0.0f); keyHistTotal = 0; keyTimer = 0; keyDetected = -1; keyPending = 0; keyOut.store (-1);
        for (auto& sec : sections)
        {
            for (auto& v : sec.voices) { v.active = false; v.fadingOut = false; std::fill (v.acc.begin(), v.acc.end(), 0.0f); v.gL = v.gR = v.mono = 0; v.airState = 0; }
            std::fill (sec.massRing.begin(), sec.massRing.end(), 0.0f);
            sec.massWrite = 0; sec.prevRef = -1.0f; sec.prevDegree = -1000; sec.levelSm = 0; sec.massGainSm = 0; sec.foldInit = false;
            sec.brL.reset(); sec.brR.reset(); sec.brHpL.reset(); sec.brHpR.reset();
            for (auto& t : sec.taps) { t.state = 0; t.apX = t.apY = 0; }
        }
        hall.clear();
        shelfL.reset(); shelfR.reset(); hpL.reset(); hpR.reset(); presL.reset(); presR.reset();
        envIn = 0;
        detectedMidi.store (-1.0f);
    }

    //--------------------------------------------------------------------------
    int activeSections() const
    {
        int n = 0;
        for (int s = 0; s < kSections; ++s) if (sectionOn (s)) ++n;
        return std::max (1, n);
    }
    bool sectionOn (int s) const { return applied.sec[(size_t) s].on && ! (s == 3 && applied.numSections < 4); }

    // Place de chaque choriste, gains, retards : recalculés quand un réglage de scène change
    void updateGeometry()
    {
        const int N = kSizes[std::clamp (applied.sizeIndex, 0, kNumSizes - 1)];
        const int cap = applied.quality <= 0 ? 8 : (applied.quality == 1 ? 16 : 32);
        const int nSec = activeSections();
        const float hum = applied.humanity;

        const float stageW = std::clamp (3.5f + 1.25f * std::sqrt ((float) N), 4.0f, 42.0f);
        const float stageD = 0.55f * stageW;
        const float listenerY = stageD + applied.distanceM;
        const float loud = 1.0f + 0.16f * std::log10 ((float) N / 3.0f);   // un grand chœur sonne un peu plus fort

        int total = 0, order = 0;
        for (int s = 0; s < kSections; ++s)
        {
            Section& sec = sections[(size_t) s];
            const SectionParams& sp = applied.sec[(size_t) s];
            const bool on = sectionOn (s);
            // répartition exacte des choristes (au moins un par pupitre allumé)
            const int per = std::max (1, N / nSec + (order < N % nSec ? 1 : 0));
            if (on) ++order;
            const int real = on ? std::min (per, cap) : 0;
            const float E = (float) per / (float) std::max (1, real);
            sec.singers = on ? per : 0;
            sec.realCount = real;
            // Puissance : choristes calculés 1/E, masse (E-1)/E. Même un petit chœur garde au moins 30 % de « densité »
            // (copies courtes décorrélées) : sinon 3 choristes identiques ne sonnent pas comme un groupe.
            float massPow = (E - 1.0f) / E;
            massPow = std::max (massPow, 0.3f);
            sec.realGain = real > 0 ? std::sqrt (1.0f - massPow) / std::sqrt ((float) real) * loud : 0.0f;
            sec.massGain = on ? std::sqrt (massPow) * loud / std::sqrt ((float) Section::kTaps) : 0.0f;
            // retards de la masse : courts pour un petit groupe serré, longs pour une foule
            const float big = std::clamp (std::log10 ((float) N / 20.0f) / std::log10 (15.0f), 0.0f, 1.0f);
            for (auto& t : sec.taps) t.delayBase = (0.006f + 0.010f * big) + (0.020f + 0.035f * big) * t.rDelay;

            const float cx = (sp.x - 0.5f) * stageW, cy = sp.y * stageD;
            const float R = std::sqrt ((float) per * 0.55f / kPi) * (0.45f + 1.1f * sp.spread) + 0.15f;

            auto place = [&] (float u, float v, float& gl, float& gr, float& dist, float& air, float& itd)
            {
                const float px = cx + u * R, py = cy + v * R * 0.62f;
                const float dx = px, dy = std::max (0.5f, listenerY - py);
                dist = std::sqrt (dx * dx + dy * dy) + 0.3f;
                const float az = std::atan2 (dx, dy);
                // Panoramique d'après la place sur toute la largeur de la scène (comme un couple de micros devant
                // le chœur), et non d'après l'angle vu de loin : sinon tout le chœur se tasse au centre (mesuré :
                // corrélation G/D +0,9, presque mono ; une référence du commerce est à -0,2).
                const float lateral = px / (0.5f * stageW);
                const float pan = std::clamp (0.85f * lateral * 1.25f + 0.15f * std::sin (az) * 1.7f, -1.0f, 1.0f);
                itd = pan * 0.00062f * (float) sr;   // l'oreille opposée entend un peu plus tard (≤ 0,62 ms)
                const float th = (pan + 1.0f) * kPi * 0.25f;
                const float g = std::clamp ((applied.distanceM + 1.5f) / dist, 0.15f, 1.6f);
                gl = std::cos (th) * g; gr = std::sin (th) * g;
                const float fc = std::clamp (19000.0f / (1.0f + dist / 20.0f), 2500.0f, 19000.0f);
                air = (float) std::exp (-2.0 * 3.141592653589793 * fc / sr);
            };

            const int ivKey = sp.interval + 100 * (int) applied.mode + 1000 * applied.keyRoot + 20000 * applied.scale;
            if (ivKey != sec.lastInterval) { sec.lastInterval = ivKey; sec.foldInit = false; }

            const float lvl = dbToGain (sp.levelDb);
            for (int i = 0; i < kMaxVoicesPerSection; ++i)
            {
                Voice& v = sec.voices[(size_t) i];
                const bool want = i < real;
                if (want)
                {
                    float gl, gr, dist, air, itd;
                    const float uu = real == 1 ? 0.0f : v.u, vv = real == 1 ? 0.0f : v.v;
                    place (uu, vv, gl, gr, dist, air, itd);
                    v.itd = itd;
                    const float ampVar = dbToGain (v.amp * 1.6f * hum);
                    v.gLt = gl * sec.realGain * ampVar * lvl;
                    v.gRt = gr * sec.realGain * ampVar * lvl;
                    v.monoT = 1.0f / std::sqrt ((float) real);
                    v.airCoef = std::pow (air, 0.65f + 0.35f * v.tone);
                    const float prop = std::max (0.0f, dist - applied.distanceM) / 343.0f * 1000.0f;
                    const float spreadMs = applied.attackMs * (0.35f + 0.65f * hum);
                    v.delayBase = (float) ((sp.delayMs + prop) * sr / 1000.0);
                    v.attackSpread = (float) (spreadMs * sr / 1000.0);
                    v.delayTarget = v.delayBase + v.attack * v.attackSpread;
                    if (! v.active || v.fadingOut) activate (v, s);
                    ++total;
                }
                else if (v.active)
                {
                    v.gLt = v.gRt = 0; v.monoT = 0; v.fadingOut = true;
                }
            }

            // masse : copies décorrélées du pupitre, réparties sur toute sa surface
            Rng r ((uint32_t) (0x9a55 + s * 17));
            for (auto& t : sec.taps)
            {
                float gl, gr, dist, air, itd;
                place (r.bi() * 0.9f, r.bi() * 0.9f, gl, gr, dist, air, itd);
                t.gL = gl; t.gR = gr;
                t.airCoef = std::min (0.995f, air * 1.03f);
            }
            sec.breathPanL = 0.55f + 0.45f * (1.0f - sp.x);
            sec.breathPanR = 0.55f + 0.45f * sp.x;
        }
        activeVoices.store (total);
    }

    void activate (Voice& v, int s)
    {
        const bool wasSilent = ! v.active;
        v.active = true;
        v.fadingOut = false;
        if (! wasSilent) return;   // il chantait encore (fondu de sortie interrompu) : on continue
        v.delay = v.delayTarget;
        const double tv = (double) (writePos - latency) - v.delay;
        v.nextSynth = std::floor (tv);
        v.cleared = (int64_t) std::floor (tv) - 2 - itdMax;
        std::fill (v.acc.begin(), v.acc.end(), 0.0f);
        v.cursor = std::max<int64_t> (0, markCount - kMarkCap);
        v.offsetInit = false;
        v.lastTarget = 1e9f;
        v.noteAge = 0;
        v.gL = v.gR = 0; v.mono = 0; v.airState = 0; v.airStateR = 0; v.itdSm = v.itd;
        for (int k = 0; k < 3; ++k) v.apxL[k] = v.apyL[k] = v.apxR[k] = v.apyR[k] = 0;
        v.lastHalfOut = Tu; v.lastTs = Tu;
        (void) s;
    }

    //--------------------------------------------------------------------------
    // Intervalle (demi-tons) d'un pupitre pour une note chantée
    // Tonalité automatique : durée de chaque classe de note chantée (oubli en ~25 s), comparée aux profils de
    // Krumhansl-Kessler des 24 tonalités ; on ne change d'avis que si la nouvelle est nettement meilleure, deux fois.
    void updateKey (float midi, float dt)
    {
        const float decay = std::exp (-dt / 60.0f);   // mémoire longue : une tonalité ne change pas toutes les 10 s
        for (auto& h : keyHist) h *= decay;
        keyHist[((int) std::lround (midi) % 12 + 12) % 12] += dt;
        keyHistTotal = keyHistTotal * decay + dt;
        keyTimer += dt;
        if (keyTimer < 0.4f || keyHistTotal < 2.0f) return;
        keyTimer = 0;
        static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float mnr[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
        auto corr = [this] (const float* prof, int root)
        {
            float mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += keyHist[i]; my += prof[(i - root + 12) % 12]; }
            mx /= 12; my /= 12;
            float sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const float a = keyHist[i] - mx, b = prof[(i - root + 12) % 12] - my;
                sxy += a * b; sxx += a * a; syy += b * b;
            }
            return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0f;
        };
        int best = 0; float bestC = -2, curC = -2;
        for (int k = 0; k < 24; ++k)
        {
            const float c = corr (k < 12 ? maj : mnr, k % 12);
            if (c > bestC) { bestC = c; best = k; }
            if (k == keyDetected) curC = c;
        }
        if (keyDetected < 0) { keyDetected = best; keyPending = 0; }
        else if (best != keyDetected && bestC > curC + 0.08f) { if (++keyPending >= 5) { keyDetected = best; keyPending = 0; } }
        else keyPending = 0;
        keyOut.store (keyDetected, std::memory_order_relaxed);
    }

    // Note « juste » la plus proche, avec hystérésis (modes intervalles et MIDI)
    static float nearestNote (float midi, float& prevRef)
    {
        if (prevRef > 0 && std::abs (midi - prevRef) < 0.75f) return prevRef;
        prevRef = std::round (midi);
        return prevRef;
    }

    float sectionOffset (int s, float midi, float& dev)
    {
        const SectionParams& sp = applied.sec[(size_t) s];
        const Register& reg = kRegisters[s];
        Section& sct = sections[(size_t) s];

        if (applied.mode == HarmonyMode::Midi)
        {
            const float ref = nearestNote (midi, sct.prevRef);
            dev = std::clamp (midi - ref, -1.5f, 1.5f);
            if (chordSize == 0) return 0.0f;   // pas d'accord tenu : unisson
            // pupitres allumés, du plus aigu au plus grave ; S prend la note la plus haute, B la plus basse
            int order = 0, count = 0;
            for (int k = 0; k < kSections; ++k) if (sectionOn (k)) { if (k < s) ++order; ++count; }
            const int idx = count > 1 ? (int) std::lround ((chordSize - 1) * (1.0 - (double) order / (count - 1))) : chordSize - 1;
            const int note = chord[std::clamp (idx, 0, chordSize - 1)];
            // même nom de note, octave la plus proche du centre de la tessiture
            float target = (float) note;
            while (target > reg.centre + 6) target -= 12;
            while (target < reg.centre - 6) target += 12;
            return std::clamp (target - ref, -36.0f, 36.0f);
        }

        const IntervalDef& iv = kIntervals[std::clamp (sp.interval, 0, kNumIntervals - 1)];
        float off, ref;
        if (applied.mode == HarmonyMode::Diatonic)
        {
            // L'intervalle vient de la gamme (tierce majeure ou mineure selon le degré), mais il s'applique au
            // demi-ton réellement chanté : une note hors gamme (ré naturel en mi majeur…) reste juste à l'unisson
            // et à l'octave, au lieu d'être tirée vers la note de gamme voisine (le pupitre chantait faux).
            int kr = applied.keyRoot, ks = applied.scale;
            if (kr == kKeyAuto) { const int k = keyDetected; kr = k < 0 ? -1 : k % 12; ks = k >= 12 ? 1 : 0; }
            off = kr < 0 ? (float) iv.semis   // tonalité pas encore trouvée : intervalles fixes
                         : diatonicOffset (midi, kr, ks, iv.steps, &sct.prevDegree);
            ref = nearestNote (midi, sct.prevRef);
        }
        else
        {
            off = (float) iv.semis;
            ref = nearestNote (midi, sct.prevRef);
        }
        dev = std::clamp (midi - ref, -1.5f, 1.5f);
        if (applied.fold)
        {
            // Une seule octave pour tout le pupitre, choisie d'après la hauteur moyenne de la phrase
            // (et revue seulement au début d'une note) : la ligne ne saute pas d'octave en pleine mélodie.
            Section& sec = sections[(size_t) s];
            if (noteStart || ! sec.foldInit)
            {
                const float a = avgMidi + off + (float) sec.foldShift;
                if (! sec.foldInit || a < reg.lo + 1.0f || a > reg.hi - 1.0f)
                {
                    int bestK = 0;
                    const float a0 = avgMidi + off;
                    if (a0 < reg.lo + 3.0f || a0 > reg.hi - 3.0f)
                    {
                        // le plus petit déplacement d'octave qui tombe bien dans la tessiture, sinon le plus proche du centre
                        bool found = false;
                        for (int step = 1; step <= 3 && ! found; ++step)
                            for (int k : { step, -step })
                            {
                                const float t = a0 + 12.0f * (float) k;
                                if (t >= reg.lo + 3.0f && t <= reg.hi - 3.0f) { bestK = k; found = true; break; }
                            }
                        if (! found)
                        {
                            float bestD = 1e9f;
                            for (int k = -3; k <= 3; ++k)
                            {
                                const float dd = std::abs (a0 + 12.0f * (float) k - reg.centre);
                                if (dd < bestD) { bestD = dd; bestK = k; }
                            }
                        }
                    }
                    sec.foldShift = 12 * bestK;
                    sec.foldInit = true;
                }
            }
            off += (float) sec.foldShift;
            // filet de sécurité pour une note très loin de la moyenne
            for (int k = 0; k < 4 && midi + off > reg.hi + 5.0f; ++k) off -= 12;
            for (int k = 0; k < 4 && midi + off < reg.lo - 5.0f; ++k) off += 12;
        }
        return off;
    }

    // Marques de période : une par cycle de la voix (sur un sommet du signal filtré), ou régulières hors voisement
    void generateMarks()
    {
        const double known = pitch.knownUntil();
        for (;;)
        {
            double T = pitch.periodAt (lastMarkPos, pitchCursor);
            const bool voicedHere = T > 0;
            double cand = lastMarkPos + (voicedHere ? T : Tu);
            const double Tc = voicedHere ? T : Tu;
            if (cand + 0.5 * Tc > known) break;
            if (cand + 1.5 * Tmax > (double) writePos) break;

            const float Tnext = pitch.periodAt (cand, pitchCursor);
            const bool voiced = Tnext > 0;
            double pos = cand;
            float period = voiced ? Tnext : (float) Tu;

            if (voiced)
            {
                // sommet le plus haut près de la position attendue (plus large à l'attaque d'une note)
                const double rad = (lastMarkVoiced ? 0.22 : 0.5) * period;
                const int64_t a = (int64_t) std::ceil (cand - rad), b = (int64_t) std::floor (cand + rad);
                float best = -1e9f; int64_t bi = (int64_t) cand;
                for (int64_t k = a; k <= b; ++k)
                {
                    const float x = inLp[(size_t) (k & inMask)];
                    if (x > best) { best = x; bi = k; }
                }
                pos = (double) bi;

                if (lastMarkVoiced)
                {
                    // Pendant une note : la marque est placée par corrélation avec la période précédente, au
                    // sous-échantillon près (le sommet seul « saute » d'un cycle à l'autre et donnait au choriste
                    // un jitter 5 fois plus fort que celui de la voix : son rugueux, robotique).
                    const int r0 = (int) std::lround (lastMarkPos);
                    const double frac0 = lastMarkPos - (double) r0;
                    const int half = std::max (8, (int) (0.5f * period));
                    const int rad2 = std::max (2, (int) (0.18f * period));
                    const int base = (int) std::lround (cand) - r0;   // décalage attendu (≈ une période)
                    const int step = period > 400 ? 2 : 1;
                    double ref2 = 0;
                    for (int k = -half; k <= half; k += step) { const float a = inLp[(size_t) ((r0 + k) & inMask)]; ref2 += (double) a * a; }
                    double bestC = -2, cPrev = 0, cNext = 0;
                    int bestL = base;
                    std::array<double, 3> win { 0, 0, 0 };
                    double prevC = -2;
                    for (int L = base - rad2; L <= base + rad2; ++L)
                    {
                        double xy = 0, y2 = 0;
                        for (int k = -half; k <= half; k += step)
                        {
                            const float a = inLp[(size_t) ((r0 + k) & inMask)], b = inLp[(size_t) ((r0 + L + k) & inMask)];
                            xy += (double) a * b; y2 += (double) b * b;
                        }
                        const double c = (ref2 > 0 && y2 > 0) ? xy / std::sqrt (ref2 * y2) : 0.0;
                        if (c > bestC) { bestC = c; bestL = L; cPrev = prevC; win[0] = prevC; win[1] = c; win[2] = -2; }
                        else if (L == bestL + 1) { win[2] = c; }
                        prevC = c;
                    }
                    cNext = win[2];
                    if (bestC > 0.6)
                    {
                        double fr = 0;
                        if (cPrev > -1.5 && cNext > -1.5)
                        {
                            const double den = cPrev - 2 * bestC + cNext;
                            if (std::abs (den) > 1e-12) fr = std::clamp (0.5 * (cPrev - cNext) / den, -0.5, 0.5);
                        }
                        const double corrPos = lastMarkPos + (double) bestL + fr;
                        (void) frac0;
                        // ancrage très lent sur le sommet, pour ne pas dériver au fil d'une longue note
                        const double drift = (double) bi - corrPos;
                        pos = corrPos + (std::abs (drift) < 0.25 * period ? 0.08 * drift : 0.0);
                    }
                }

                const double minGap = 0.55 * period;
                if (pos < lastMarkPos + minGap) pos = lastMarkPos + minGap;
                // la période de la marque : l'écart réel aux marques voisines quand la note continue
                if (lastMarkVoiced)
                {
                    const double gap = pos - lastMarkPos;
                    if (gap > 0.8 * period && gap < 1.25 * period) period = (float) (0.5 * (gap + period));
                }
            }

            Mark& m = marks[(size_t) (markCount % kMarkCap)];
            m.pos = pos;
            m.period = period;
            m.voiced = voiced;
            m.midi = voiced ? (float) (69.0 + 12.0 * std::log2 (sr / ((double) period * 440.0))) : -1.0f;
            const Mark* prev = markCount > 0 ? &marks[(size_t) ((markCount - 1) % kMarkCap)] : nullptr;
            if (voiced) updateKey (m.midi, period / (float) sr);
            if (voiced)
            {
                // hauteur moyenne (≈ 2,5 s) et début de note
                if (avgMidi < 0) avgMidi = m.midi;
                else avgMidi += (m.midi - avgMidi) * std::min (1.0f, period / (float) sr / 2.5f);
                noteStart = ! lastMarkVoiced || (prev != nullptr && std::abs (m.midi - prev->midi) > 0.8f);
            }
            for (int s = 0; s < kSections; ++s)
            {
                if (voiced)
                {
                    // Les choristes suivent la justesse LENTE du chanteur (glissés, attaques par en dessous, note un peu
                    // basse : 90 %), pas ses variations rapides (vibrato, micro-variations : 20 %), qu'ils remplacent par
                    // les leurs. Tout suivre = parallèle robotique ; ne rien suivre = chœur faux par rapport à la voix.
                    Section& sc = sections[(size_t) s];
                    const float refBefore = sc.prevRef;
                    m.offset[s] = sectionOffset (s, m.midi, m.dev[s]);
                    const float raw = m.dev[s];
                    if (sc.prevRef != refBefore || ! lastMarkVoiced) sc.devSlow = raw;
                    else sc.devSlow += (raw - sc.devSlow) * std::min (1.0f, period / (float) sr / 0.08f);
                    const float followed = 0.9f * sc.devSlow + 0.2f * (raw - sc.devSlow);
                    m.dev[s] = raw - followed;
                }
                else { m.offset[s] = prev != nullptr ? prev->offset[s] : 0.0f; m.dev[s] = prev != nullptr ? prev->dev[s] : 0.0f; }
            }
            if (voiced) detectedMidi.store (m.midi, std::memory_order_relaxed);
            ++markCount;
            lastMarkPos = pos;
            lastMarkVoiced = voiced;
        }
    }

    inline float readIn (double pos) const   // interpolation cubique (Hermite)
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float f = (float) (pos - fl);
        const float xm1 = inMono[(size_t) ((i - 1) & inMask)], x0 = inMono[(size_t) (i & inMask)];
        const float x1 = inMono[(size_t) ((i + 1) & inMask)], x2 = inMono[(size_t) ((i + 2) & inMask)];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    // Un grain PSOLA pour le choriste v du pupitre s
    void spawnGrain (Voice& v, int s, float formantBase)
    {
        const double sp = v.nextSynth;
        if (markCount == 0) { v.nextSynth += Tu; v.lastHalfOut = Tu; v.lastTs = Tu; return; }

        const int64_t oldest = std::max<int64_t> (0, markCount - kMarkCap);
        if (v.cursor < oldest) v.cursor = oldest;
        while (v.cursor + 1 < markCount && marks[(size_t) ((v.cursor + 1) % kMarkCap)].pos <= sp) ++v.cursor;
        const Mark* m = &marks[(size_t) (v.cursor % kMarkCap)];
        if (v.cursor + 1 < markCount)
        {
            const Mark& m2 = marks[(size_t) ((v.cursor + 1) % kMarkCap)];
            if (std::abs (m2.pos - sp) < std::abs (m->pos - sp)) m = &m2;
        }

        const float hum = applied.humanity;
        const float dt = (float) (v.lastTs / sr);
        const float fF = std::clamp (formantBase * (1.0f + 0.09f * hum * v.formant), 0.6f, 1.7f);

        double leftOut, rightOut, centre, Ts;
        float g;
        float resample = 1.0f;   // lecture accélérée (non voisé près d'une note)
        if (m->voiced)
        {
            const float target = m->offset[s];
            if (! v.offsetInit) { v.offsetSm = target; v.offsetInit = true; }
            if (v.inGap) v.phraseAge = 0.0f;   // nouvelle phrase : le choriste entre en fondu, à son rythme
            v.inGap = false;
            if (std::abs (target - v.lastTarget) > 0.5f) v.noteAge = 0;
            v.lastTarget = target;
            const float glide = v.glideTau * (0.4f + 0.9f * hum);
            v.offsetSm += (target - v.offsetSm) * (dt / (glide + dt));
            v.noteAge += dt;

            // dérive lente de justesse
            v.driftTimer -= dt;
            if (v.driftTimer <= 0) { v.driftTarget = v.rng.gauss() * applied.detuneCents * 0.006f * hum; v.driftTimer = 0.35f + 1.1f * v.rng.uni(); }
            v.drift += (v.driftTarget - v.drift) * (dt / (0.4f + dt));

            // vibrato vivant : la vitesse et l'amplitude flottent un peu, comme chez un chanteur
            v.vibWander += (v.rng.bi() * 0.5f - v.vibWander * 0.02f) * dt * 6.0f;
            v.vibWander = std::clamp (v.vibWander, -1.0f, 1.0f);
            v.vibPhase += 2 * kPi * v.vibRate * (1.0f + 0.07f * v.vibWander) * dt;
            if (v.vibPhase > 2 * kPi) v.vibPhase -= 2 * kPi;
            const float env = std::clamp ((v.noteAge - 0.15f) / 0.5f, 0.0f, 1.0f);
            const float vib = applied.vibrato * 0.38f * v.vibDepth * (1.0f + 0.25f * v.vibWander) * env * std::sin (v.vibPhase);

            // Le choriste ne recopie pas le vibrato ni les glissés de la voix principale : il chante la note juste,
            // avec sa justesse lente mais pas son vibrato (voir generateMarks), et son propre vibrato, sa propre dérive.
            const float semis = v.offsetSm - m->dev[s] + v.detune * applied.detuneCents * 0.01f + v.drift + vib;
            const double r = std::clamp ((double) std::exp2 (semis / 12.0f), 0.25, 4.0);
            const double Ta = m->period;
            // Vers le grave, le grain devient asymétrique : il commence juste avant l'impulsion de la glotte
            // et ne garde plus la fin de la période précédente (sinon l'octave inférieure reste faible :
            // on entendrait surtout la note d'origine). À l'octave au-dessous : 0,3 période avant, 1 après.
            const double down = std::clamp ((r - 0.5) / 0.5, 0.0, 1.0);
            const double leftIn = Ta * (0.3 + 0.7 * down), rightIn = Ta;
            leftOut = leftIn / fF;
            rightOut = rightIn / fF;
            centre = m->pos;
            Ts = Ta / r;
            g = (float) (std::sqrt (fF / r) * std::sqrt (2.0 * Ta / (leftIn + rightIn)));
            v.ampPhase += 2 * kPi * v.ampRate * dt;
            if (v.ampPhase > 2 * kPi) v.ampPhase -= 2 * kPi;
            g *= 1.0f + 0.16f * hum * std::sin (v.ampPhase);
            if (v.phraseAge < v.rampTime)
            {
                const float x = v.phraseAge / v.rampTime;
                g *= x * x * (3.0f - 2.0f * x);
            }
            v.phraseAge += dt;
        }
        else
        {
            // Non voisé. Juste avant ou après une note (attaque pas encore détectée, fin de note), le grain est
            // transposé de l'intervalle de cette note voisine : sinon le pupitre chanterait un instant la note
            // de la voix principale avant de rejoindre la sienne. Plus loin (consonnes, souffle) : copie simple.
            v.noteAge = 0;
            leftOut = rightOut = Tu;
            centre = sp;
            Ts = Tu;
            g = 0.3f;   // consonnes et souffle : discrets chez les choristes (la voix principale les porte)
            if (! v.inGap)
            {
                // nouvelle phrase : chaque choriste attaque un peu différemment de la fois précédente
                v.inGap = true;
                v.delayTarget = v.delayBase + v.rng.uni() * v.attackSpread;
            }
            const double near = 0.05 * sr, nearBack = 0.15 * sr;   // attaque : 50 ms ; fin de note : 150 ms
            const Mark* nb = nullptr;
            for (int64_t c = v.cursor + 1; c < markCount && c <= v.cursor + 16; ++c)
            {
                const Mark& mk = marks[(size_t) (c % kMarkCap)];
                if (mk.pos - sp > near) break;
                if (mk.voiced) { nb = &mk; break; }
            }
            if (nb == nullptr)
                for (int64_t c = v.cursor; c >= oldest && c >= v.cursor - 32; --c)
                {
                    const Mark& mk = marks[(size_t) (c % kMarkCap)];
                    if (sp - mk.pos > nearBack) break;
                    if (mk.voiced) { nb = &mk; break; }
                }
            if (nb != nullptr)
            {
                const float semis = (v.offsetInit ? v.offsetSm : nb->offset[s]) - nb->dev[s] + v.detune * applied.detuneCents * 0.01f;
                resample = std::clamp (std::exp2 (semis / 12.0f), 0.25f, 4.0f);
                g = 0.8f;
            }
        }

        const int64_t n0 = (int64_t) std::ceil (sp - leftOut), n1 = (int64_t) std::floor (sp + rightOut);
        const float invL = (float) (1.0 / leftOut), invR = (float) (1.0 / rightOut);
        const double newest = (double) writePos - 3.0;
        for (int64_t n = std::max (n0, v.cleared + 1); n <= n1; ++n)
        {
            const double k = (double) n - sp;
            const float w = hann ((float) k * (k < 0 ? invL : invR));
            double src = centre + k * fF * resample;
            if (src > newest) src = newest;
            v.acc[(size_t) (n & v.accMask)] += g * w * readIn (src);
        }
        v.nextSynth += Ts;
        v.lastHalfOut = leftOut;   // le grain suivant commence environ à cette distance avant son centre
        v.lastTs = Ts;
    }

    float sectionFormant (int s) const
    {
        const bool female = s < 2;
        const float regTweak[kSections] = { 1.04f, 0.99f, 1.02f, 0.95f };
        float f = (female ? 1.17f : 1.0f) / (applied.sourceFemale ? 1.17f : 1.0f) * regTweak[s];
        f *= std::exp2 (applied.sec[(size_t) s].formant * 0.5f);   // ±6 demi-tons
        return f;
    }

    void processVoice (Voice& v, int s, int n, int64_t blockStart, float formantBase)
    {
        // le choriste chante dans le passé : latence + son retard (pupitre, attaque, distance)
        // vitesse max de variation du retard : ≈ 4 cents pendant une note, rapide dans les silences
        const double rate = v.inGap ? 0.05 : 0.0025;
        const double tEndEst = (double) (blockStart + n - latency) - (v.delay - rate * n) + 2.0;
        int guard = 0;
        while (v.nextSynth - v.lastHalfOut <= tEndEst && guard++ < 4096) spawnGrain (v, s, formantBase);

        float* L = busL.data();
        float* R = busR.data();
        float* M = mono.data();
        double tv = 0;
        for (int i = 0; i < n; ++i)
        {
            const double dd = (double) v.delayTarget - v.delay;
            v.delay += std::clamp (dd, -rate, rate);
            tv = (double) (blockStart + i - latency) - v.delay;
            const double fl = std::floor (tv);
            const int64_t i0 = (int64_t) fl;
            const float fr = (float) (tv - fl);
            const float a = v.acc[(size_t) (i0 & v.accMask)], b = v.acc[(size_t) ((i0 + 1) & v.accMask)];
            const float y = a + fr * (b - a);
            // écart interaural : l'oreille du côté opposé lit le même choriste un peu plus tôt dans le passé
            v.itdSm += (v.itd - v.itdSm) * 0.001f;
            const double tl = tv - (double) std::max (0.0f, -v.itdSm), tr = tv - (double) std::max (0.0f, v.itdSm);
            auto readAcc = [&v] (double t) {
                const double f0 = std::floor (t); const int64_t k = (int64_t) f0; const float q = (float) (t - f0);
                const float p0 = v.acc[(size_t) (k & v.accMask)], p1 = v.acc[(size_t) ((k + 1) & v.accMask)];
                return p0 + q * (p1 - p0);
            };
            float yl = v.itdSm < 0 ? readAcc (tl) : y;
            float yr = v.itdSm > 0 ? readAcc (tr) : y;
            for (int k = 0; k < 3; ++k)
            {
                const float c = v.apc[k];
                const float ol = c * yl + v.apxL[k] - c * v.apyL[k]; v.apxL[k] = yl; v.apyL[k] = ol; yl = ol;
                const float orr = c * yr + v.apxR[k] - c * v.apyR[k]; v.apxR[k] = yr; v.apyR[k] = orr; yr = orr;
            }

            v.gL += (v.gLt - v.gL) * gainSmCoef;
            v.gR += (v.gRt - v.gR) * gainSmCoef;
            v.mono += (v.monoT - v.mono) * gainSmCoef;
            v.airState = yl + v.airCoef * (v.airState - yl);
            v.airStateR = yr + v.airCoef * (v.airStateR - yr);
            L[i] += v.airState * v.gL;
            R[i] += v.airStateR * v.gR;
            M[i] += y * v.mono;
        }
        // on efface ce qui a été lu
        const int64_t upTo = (int64_t) std::floor (tv) - 2 - itdMax;   // on garde de quoi lire l'écart interaural
        for (int64_t k = v.cleared + 1; k <= upTo; ++k) v.acc[(size_t) (k & v.accMask)] = 0.0f;
        if (upTo > v.cleared) v.cleared = upTo;

        if (v.fadingOut && std::abs (v.gL) + std::abs (v.gR) < 1e-5f) { v.active = false; v.fadingOut = false; }
    }

    //--------------------------------------------------------------------------
    void process (const float* const* in, int numIn, float* const* out, int numOut, int n)
    {
        // réglages
        if (firstParams || paramsChanged())
        {
            const bool first = firstParams;
            applied = p; firstParams = false;
            hall.setHall (applied.hall); hall.setPredelay (applied.predelayMs);
            shelfL.setHighShelf (sr, kAirHz, applied.brightnessDb);
            shelfR.setHighShelf (sr, kAirHz, applied.brightnessDb);
            if (first)
            {
                hpL.set (sr, kLowCutHz, 0.6, 1); hpR.set (sr, kLowCutHz, 0.6, 1);
                presL.setHighShelf (sr, kPresenceHz, kPresenceDb); presR.setHighShelf (sr, kPresenceHz, kPresenceDb);
                shelfL.reset(); shelfR.reset(); hpL.reset(); hpR.reset(); presL.reset(); presR.reset();
            }
            updateGeometry();
        }
        if (chordDirty) { rebuildChord(); chordDirty = false; }

        const int64_t blockStart = writePos;

        // 1. entrée : mono, filtrée pour les marques, retard de la voix principale, analyse
        float peakIn = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = in[0][i];
            const float r = numIn > 1 ? in[1][i] : l;
            const float x = 0.5f * (l + r);
            peakIn = std::max (peakIn, std::abs (x));
            lp1 = x + lpCoef * (lp1 - x);
            lp2 = lp1 + lpCoef * (lp2 - lp1);
            const size_t idx = (size_t) (writePos & inMask);
            inMono[idx] = x;
            inLp[idx] = lp2;
            dryL[idx] = l; dryR[idx] = r;
            ++writePos;
            if (pitch.push (x, writePos)) generateMarks();
        }
        generateMarks();
        inLevel.store (peakIn, std::memory_order_relaxed);
        if (! pitch.voiced) { /* la dernière note reste affichée un instant */ }

        // 2. pupitres
        float* L = busL.data();
        float* R = busR.data();
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);

        for (int s = 0; s < kSections; ++s)
        {
            Section& sec = sections[(size_t) s];
            float* M = mono.data();
            std::fill (M, M + n, 0.0f);
            const float fBase = sectionFormant (s);
            bool any = false;
            for (auto& v : sec.voices)
                if (v.active) { processVoice (v, s, n, blockStart, fBase); any = true; }

            // masse et souffle
            const float lvl = sectionOn (s) ? dbToGain (applied.sec[(size_t) s].levelDb) : 0.0f;
            const float massTarget = sec.massGain * lvl;
            const float breathTarget = sectionOn (s) ? applied.breath * 0.05f * lvl * std::sqrt ((float) std::max (1, sec.singers)) / std::sqrt (1.0f + (float) sec.singers * 0.02f) : 0.0f;
            if (! any && sec.massGainSm < 1e-6f && sec.levelSm < 1e-6f && massTarget <= 0.0f && breathTarget <= 0.0f) continue;

            const float sr_f = (float) sr;
            for (int i = 0; i < n; ++i)
            {
                sec.massRing[(size_t) (sec.massWrite & sec.massMask)] = M[i];
                sec.massGainSm += (massTarget - sec.massGainSm) * gainSmCoef;
                if (sec.massGainSm > 1e-6f)
                {
                    float ml = 0, mr = 0;
                    for (auto& t : sec.taps)
                    {
                        t.phase += 2 * kPi * t.rate / sr_f;
                        if (t.phase > 2 * kPi) t.phase -= 2 * kPi;
                        t.phase2 += 2 * kPi * t.rate2 / sr_f;
                        if (t.phase2 > 2 * kPi) t.phase2 -= 2 * kPi;
                        const float dSamp = (t.delayBase + t.depth * (0.6f * std::sin (t.phase) + 0.4f * std::sin (t.phase2))) * sr_f;
                        const float rp = (float) sec.massWrite - dSamp;
                        const float fl = std::floor (rp);
                        const int64_t ip = (int64_t) fl;
                        const float fr = rp - fl;
                        const float a = sec.massRing[(size_t) (ip & sec.massMask)], b = sec.massRing[(size_t) ((ip + 1) & sec.massMask)];
                        const float y0 = a + fr * (b - a);
                        const float y = t.ap * y0 + t.apX - t.ap * t.apY;   // passe-tout du 1er ordre
                        t.apX = y0; t.apY = y;
                        t.state = y + t.airCoef * (t.state - y);
                        ml += t.state * t.gL;
                        mr += t.state * t.gR;
                    }
                    L[i] += ml * sec.massGainSm;
                    R[i] += mr * sec.massGainSm;
                }
                ++sec.massWrite;

                // souffle : bruit qui suit l'enveloppe de la voix (avec le même retard que le chœur)
                const float xd = std::abs (inMono[(size_t) ((blockStart + i - latency) & inMask)]);
                if (s == 0) envIn = xd > envIn ? xd + envCoefA * (envIn - xd) : xd + envCoefR * (envIn - xd);
                sec.levelSm += (breathTarget - sec.levelSm) * gainSmCoef;
                if (sec.levelSm > 1e-6f)
                {
                    const float e = sec.levelSm * envIn;
                    L[i] += sec.brL.process (sec.brHpL.process (sec.noise.bi())) * e * sec.breathPanL;
                    R[i] += sec.brR.process (sec.brHpR.process (sec.noise.bi())) * e * sec.breathPanR;
                }
            }
        }

        // 3. acoustique, largeur, voix principale, sortie
        const float leadT = applied.leadDb <= -59.9f ? 0.0f : dbToGain (applied.leadDb);
        const float choirT = dbToGain (applied.choirDb);
        const float outT = dbToGain (applied.gainDb);
        const float wetT = applied.reverb * (0.55f + applied.distanceM / 14.0f) * 0.55f;
        const float widthT = applied.width;
        float peakOut = 0;
        for (int i = 0; i < n; ++i)
        {
            leadSm += (leadT - leadSm) * gainSmCoef;
            choirSm += (choirT - choirSm) * gainSmCoef;
            outSm += (outT - outSm) * gainSmCoef;
            wetSm += (wetT - wetSm) * gainSmCoef;
            widthSm += (widthT - widthSm) * gainSmCoef;

            L[i] = shelfL.process (presL.process (hpL.process (L[i])));
            R[i] = shelfR.process (presR.process (hpR.process (R[i])));
            float wl, wr;
            hall.process (L[i], R[i], wl, wr);
            float cl = L[i] * (1.0f - 0.3f * wetSm) + wl * wetSm;
            float cr = R[i] * (1.0f - 0.3f * wetSm) + wr * wetSm;
            const float mid = 0.5f * (cl + cr), side = 0.5f * (cl - cr) * widthSm;
            cl = mid + side; cr = mid - side;

            const size_t di = (size_t) ((blockStart + i - latency) & inMask);
            const float ol = (dryL[di] * leadSm + cl * choirSm) * outSm;
            const float orr = (dryR[di] * leadSm + cr * choirSm) * outSm;
            if (numOut > 1) { out[0][i] = ol; out[1][i] = orr; }
            else out[0][i] = 0.5f * (ol + orr);
            peakOut = std::max (peakOut, std::max (std::abs (ol), std::abs (orr)));
        }
        outLevel.store (peakOut, std::memory_order_relaxed);
    }

    static bool ne (float a, float b) { return std::abs (a - b) > 1e-7f; }

    bool paramsChanged() const
    {
        const Params& a = p; const Params& b = applied;
        if (a.sizeIndex != b.sizeIndex || a.mode != b.mode || a.keyRoot != b.keyRoot || a.scale != b.scale
            || a.sourceFemale != b.sourceFemale || a.numSections != b.numSections || a.quality != b.quality || a.fold != b.fold
            || ne (a.detuneCents, b.detuneCents) || ne (a.attackMs, b.attackMs) || ne (a.vibrato, b.vibrato) || ne (a.breath, b.breath)
            || ne (a.humanity, b.humanity) || a.hall != b.hall || ne (a.distanceM, b.distanceM) || ne (a.reverb, b.reverb)
            || ne (a.predelayMs, b.predelayMs) || ne (a.leadDb, b.leadDb) || ne (a.choirDb, b.choirDb) || ne (a.width, b.width) || ne (a.gainDb, b.gainDb) || ne (a.brightnessDb, b.brightnessDb))
            return true;
        for (int s = 0; s < kSections; ++s)
        {
            const SectionParams& x = a.sec[(size_t) s]; const SectionParams& y = b.sec[(size_t) s];
            if (x.on != y.on || x.interval != y.interval || ne (x.levelDb, y.levelDb) || ne (x.spread, y.spread) || ne (x.formant, y.formant)
                || ne (x.delayMs, y.delayMs) || ne (x.x, y.x) || ne (x.y, y.y))
                return true;
        }
        return false;
    }

    void rebuildChord()
    {
        int k = 0;
        for (int i = 0; i < 128; ++i) if (held[i]) chord[k++] = i;
        if (k > 0) chordSize = k;   // accord « verrouillé » : on garde le dernier quand on relâche tout
    }
};

//==============================================================================
Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;
void Engine::prepare (double sr, int block) { impl->prepare (sr, block); }
void Engine::reset() { impl->reset(); }
int Engine::getLatencySamples() const { return impl->latency; }
void Engine::setParams (const Params& p) { impl->p = p; }
void Engine::noteOn (int n) { if (n >= 0 && n < 128) { impl->held[n] = true; impl->chordDirty = true; } }
void Engine::noteOff (int n) { if (n >= 0 && n < 128) { impl->held[n] = false; impl->chordDirty = true; } }
void Engine::allNotesOff() { std::fill (std::begin (impl->held), std::end (impl->held), false); impl->chordDirty = true; }
void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    int done = 0;
    while (done < n)
    {
        const int chunk = std::min (impl->maxBlock, n - done);
        const float* ins[2] = { in[0] + done, numIn > 1 ? in[1] + done : in[0] + done };
        float* outs[2] = { out[0] + done, numOut > 1 ? out[1] + done : out[0] + done };
        impl->process (ins, numIn, outs, numOut, chunk);
        done += chunk;
    }
}
float Engine::getDetectedMidi() const { return impl->detectedMidi.load (std::memory_order_relaxed); }
int Engine::getActiveVoices() const { return impl->activeVoices.load (std::memory_order_relaxed); }
float Engine::getInputLevel() const { return impl->inLevel.load (std::memory_order_relaxed); }
float Engine::getOutputLevel() const { return impl->outLevel.load (std::memory_order_relaxed); }
int Engine::getDetectedKey() const { return impl->keyOut.load (std::memory_order_relaxed); }
} // namespace choir
