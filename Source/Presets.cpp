#include "Presets.h"
#include "ParamIDs.h"

namespace presets
{
namespace
{
struct Value { juce::String id; float v; };
struct Preset { const char* name; std::vector<Value> values; };

// Intervalles : 0 unisson, 2 tierce ↑, 4 quinte ↑, 7 octave ↑, 9 tierce ↓, 10 quarte ↓, 11 quinte ↓, 12 sixte ↓, 14 octave ↓, 16 octave + quinte ↓
// Tailles : 0 = 3, 1 = 5, 2 = 10, 3 = 20, 4 = 50, 5 = 100, 6 = 300, 7 = 600, 8 = 1000
// Lieux : 0 Studio, 1 Chapelle, 2 Église, 3 Cathédrale
const std::vector<Preset>& all()
{
    using namespace ids;
    static const std::vector<Preset> list = {
        { "Chœur de cathédrale", {} },   // = réglages par défaut
        { "Afro / Gospel studio", { { size, 2 }, { voices, 0 }, { hall, 0 }, { distance, 3 }, { reverb, 30 }, { attack, 8 }, { vibrato, 45 },
                                    { width, 150 }, { brightness, 4 }, { lead, -6 }, { fold, 0 },
                                    { sec (0, interval), 2 }, { sec (1, interval), 0 }, { sec (2, interval), 14 },
                                    { sec (0, x), 0.05f }, { sec (0, y), 0.6f }, { sec (1, x), 0.95f }, { sec (1, y), 0.6f },
                                    { sec (2, x), 0.25f }, { sec (2, y), 0.35f },
                                    { sec (0, delay), 3 }, { sec (1, delay), 5 }, { sec (2, delay), 7 } } },
        { "Grand chœur de cathédrale", { { size, 6 }, { hall, 3 }, { distance, 14 }, { reverb, 45 }, { attack, 45 } } },
        { "Chœur symphonique", { { size, 5 }, { hall, 2 }, { distance, 10 }, { reverb, 35 } } },
        { "Chœur de chambre", { { size, 3 }, { hall, 2 }, { distance, 6 }, { reverb, 28 }, { attack, 30 }, { humanity, 75 } } },
        { "Petit ensemble vocal", { { size, 2 }, { hall, 1 }, { distance, 4 }, { reverb, 22 }, { attack, 25 }, { vibrato, 45 } } },
        { "Trio de choristes (studio)", { { size, 0 }, { voices, 0 }, { hall, 0 }, { distance, 2 }, { reverb, 12 }, { attack, 18 },
                                          { sec (0, interval), 2 }, { sec (1, interval), 0 }, { sec (2, interval), 9 }, { fold, 0 } } },
        { "Gospel", { { size, 3 }, { voices, 0 }, { hall, 0 }, { distance, 3 }, { reverb, 15 }, { vibrato, 70 }, { attack, 20 },
                      { sec (0, interval), 2 }, { sec (1, interval), 0 }, { sec (2, interval), 9 }, { fold, 0 } } },
        { "Chœur d'hommes", { { size, 4 }, { hall, 2 }, { distance, 8 }, { reverb, 35 },
                              { sec (0, on), 0 }, { sec (1, on), 0 }, { sec (2, interval), 0 }, { sec (3, interval), 14 } } },
        { "Chœur de femmes", { { size, 4 }, { hall, 2 }, { distance, 8 }, { reverb, 35 },
                               { sec (0, interval), 0 }, { sec (1, interval), 9 }, { sec (2, on), 0 }, { sec (3, on), 0 } } },
        { "Moines (plain-chant)", { { size, 3 }, { hall, 3 }, { distance, 12 }, { reverb, 50 }, { vibrato, 10 }, { breath, 30 },
                                    { sec (0, on), 0 }, { sec (1, on), 0 }, { sec (2, interval), 0 }, { sec (3, interval), 14 } } },
        { "Unisson massif (1000 voix)", { { size, 8 }, { hall, 3 }, { distance, 18 }, { reverb, 45 },
                                          { sec (0, interval), 0 }, { sec (1, interval), 0 }, { sec (2, interval), 0 }, { sec (3, interval), 0 } } },
        { "Accords au clavier (MIDI)", { { size, 5 }, { mode, 2 }, { hall, 2 }, { distance, 9 }, { reverb, 35 } } },
        { "Quintes médiévales", { { size, 4 }, { hall, 3 }, { distance, 12 }, { reverb, 45 }, { mode, 1 },
                                  { sec (0, interval), 7 }, { sec (1, interval), 4 }, { sec (2, interval), 0 }, { sec (3, interval), 11 }, { fold, 0 } } },
    };
    return list;
}
} // namespace

int count() { return (int) all().size(); }

juce::String name (int index)
{
    if (index < 0 || index >= count()) return {};
    return juce::String (juce::CharPointer_UTF8 (all()[(size_t) index].name));
}

void apply (juce::AudioProcessorValueTreeState& apvts, int index)
{
    if (index < 0 || index >= count()) return;
    // tout revient au réglage par défaut (sauf la voix d'origine, la tonalité et la qualité, propres au morceau et à l'ordinateur)
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (id == ids::source || id == ids::key || id == ids::scale || id == ids::quality)
                continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    for (const auto& v : all()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (v.id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v.v));
}
} // namespace presets
