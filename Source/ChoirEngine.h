// Chènevières Chorale — moteur audio (sans JUCE, testable seul)
//
// Une voix entre, un chœur sort :
//   1. détection de hauteur (YIN sur signal décimé) et marques de période (PSOLA) ;
//   2. pour chaque pupitre, l'intervalle à chanter (diatonique, fixe ou MIDI) ;
//   3. des « vrais » choristes synthétisés (PSOLA avec formants), chacun avec son désaccord,
//      sa dérive, son vibrato, son retard d'attaque, son timbre et sa place sur la scène ;
//   4. au-delà de la capacité de calcul, une « masse » de copies décorrélées qui épaissit le pupitre ;
//   5. souffle, acoustique (réverbération à réseau de retards) et sortie stéréo.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace choir
{
constexpr int kSections = 4;               // Soprano, Alto, Ténor, Basse
constexpr int kMaxVoicesPerSection = 32;    // choristes réellement synthétisés (qualité Haute)
constexpr int kNumSizes = 9;
extern const int kSizes[kNumSizes];         // 3 … 1000 choristes

struct IntervalDef { int steps; int semis; };  // degrés (mode diatonique) et demi-tons (intervalles fixes)
constexpr int kNumIntervals = 18;
extern const IntervalDef kIntervals[kNumIntervals];

constexpr int kNumScales = 5;               // Majeur, mineur naturel, mineur harmonique, dorien, mixolydien
constexpr int kNumHalls = 4;                // Studio, Chapelle, Église, Cathédrale

enum class HarmonyMode { Diatonic = 0, Fixed = 1, Midi = 2 };

struct SectionParams
{
    bool on = true;
    int interval = 0;          // index dans kIntervals
    float levelDb = 0.0f;      // -24 … +6
    float spread = 0.6f;       // 0 … 1 : étendue du pupitre sur la scène
    float formant = 0.0f;      // -1 … +1 (0 = automatique, ±1 = ±6 demi-tons)
    float delayMs = 20.0f;     // 0 … 100 : le pupitre chante un peu après la voix principale
    float x = 0.5f, y = 0.5f;  // place sur la scène (0 = gauche / fond, 1 = droite / devant)
};

struct Params
{
    int sizeIndex = 5;                       // 100 choristes
    HarmonyMode mode = HarmonyMode::Diatonic;
    int keyRoot = 2;                         // ré
    int scale = 1;                           // mineur naturel
    bool sourceFemale = false;               // timbre de la voix d'origine
    int numSections = 4;                     // 3 ou 4 pupitres (3 : la basse se tait)
    int quality = 1;                         // 0 Éco (8), 1 Normale (16), 2 Haute (32 choristes réels par pupitre)
    bool fold = true;                        // ramener chaque pupitre dans sa tessiture

    float detuneCents = 9.0f;                // 0 … 30
    float attackMs = 25.0f;                  // 0 … 150
    float vibrato = 0.55f;                   // 0 … 1
    float breath = 0.2f;                     // 0 … 1
    float humanity = 0.7f;                   // 0 … 1

    int hall = 3;                            // cathédrale
    float distanceM = 8.0f;                  // 1 … 40
    float reverb = 0.35f;                    // 0 … 1
    float predelayMs = 24.0f;                // 0 … 120

    float leadDb = -3.0f;                    // voix principale (≤ -60 : coupée)
    float choirDb = 0.0f;
    float width = 1.0f;                      // 0 … 1.5
    float gainDb = 0.0f;
    float brightnessDb = 3.0f;               // brillance du chœur (étagère aiguë à 6,5 kHz, -6 … +9 dB)

    std::array<SectionParams, kSections> sec;

    Params();
};

// Tessiture de chaque pupitre (notes MIDI) : basse, haute, centre
struct Register { float lo, hi, centre; };
extern const Register kRegisters[kSections];

// Intervalle (en demi-tons) que chante un pupitre pour la note chantée `midi` — utile aux tests
float diatonicOffset (float midi, int keyRoot, int scale, int steps, int* prevDegree);

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    int getLatencySamples() const;

    void setParams (const Params& p);         // fil audio, avant process()
    void noteOn (int note);
    void noteOff (int note);
    void allNotesOff();

    // in : 1 ou 2 canaux ; out : 1 ou 2 canaux (peut être le même tampon que in)
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    // Lectures pour l'interface (n'importe quel fil)
    float getDetectedMidi() const;            // < 0 : pas de note
    int getActiveVoices() const;              // choristes réellement calculés
    float getInputLevel() const;
    float getOutputLevel() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl;
};
} // namespace choir
