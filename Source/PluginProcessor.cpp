#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Presets.h"

using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;

namespace
{
juce::ParameterID pid (const juce::String& id) { return { id, 1 }; }

juce::NormalisableRange<float> range (float lo, float hi, float step = 0.0f, float centre = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > lo && centre < hi) r.setSkewForCentre (centre);
    return r;
}

std::unique_ptr<APF> pct (const juce::String& id, const juce::String& name, float def, float lo = 0, float hi = 100)
{
    return std::make_unique<APF> (pid (id), name, range (lo, hi, 1.0f), def,
                                  juce::AudioParameterFloatAttributes().withLabel ("%"));
}

std::unique_ptr<APF> dbParam (const juce::String& id, const juce::String& name, float lo, float hi, float def, bool offAtMin = false)
{
    auto attrs = juce::AudioParameterFloatAttributes().withLabel ("dB")
        .withStringFromValueFunction ([offAtMin, lo] (float v, int) {
            if (offAtMin && v <= lo + 0.05f) return juce::String (juce::CharPointer_UTF8 ("Coupée"));
            return juce::String (v, 1);
        });
    return std::make_unique<APF> (pid (id), name, range (lo, hi, 0.1f), def, attrs);
}

std::unique_ptr<APF> msParam (const juce::String& id, const juce::String& name, float lo, float hi, float def, float centre = 0)
{
    return std::make_unique<APF> (pid (id), name, range (lo, hi, 0.1f, centre), def,
                                  juce::AudioParameterFloatAttributes().withLabel ("ms"));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ChoraleProcessor::createLayout()
{
    using namespace ids;
    using names::u;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const choir::Params def;

    layout.add (std::make_unique<APC> (pid (size), u ("Taille du chœur"), names::sizes(), def.sizeIndex));
    layout.add (std::make_unique<APC> (pid (mode), u ("Harmonie"), names::modes(), (int) def.mode));
    layout.add (std::make_unique<APC> (pid (key), u ("Tonalité"), names::keys(), def.keyRoot));
    layout.add (std::make_unique<APC> (pid (scale), u ("Mode"), names::scales(), def.scale));
    layout.add (std::make_unique<APC> (pid (source), u ("Voix d'origine"), names::sources(), def.sourceFemale ? 1 : 0));
    layout.add (std::make_unique<APC> (pid (voices), u ("Pupitres"), names::voiceCounts(), def.numSections == 4 ? 1 : 0));
    layout.add (std::make_unique<APC> (pid (quality), u ("Qualité"), names::qualities(), def.quality));
    layout.add (std::make_unique<APB> (pid (fold), u ("Tessiture automatique"), def.fold));

    layout.add (std::make_unique<APF> (pid (detune), u ("Désaccord"), range (0, 30, 0.1f), def.detuneCents,
                                       juce::AudioParameterFloatAttributes().withLabel ("cents")));
    layout.add (msParam (attack, u ("Attaques"), 0, 150, def.attackMs));
    layout.add (pct (vibrato, u ("Vibrato"), def.vibrato * 100));
    layout.add (pct (breath, u ("Souffle"), def.breath * 100));
    layout.add (pct (humanity, u ("Humanité"), def.humanity * 100));

    layout.add (std::make_unique<APC> (pid (hall), u ("Lieu"), names::halls(), def.hall));
    layout.add (std::make_unique<APF> (pid (distance), u ("Distance"), range (1, 40, 0.1f, 8), def.distanceM,
                                       juce::AudioParameterFloatAttributes().withLabel ("m")));
    layout.add (pct (reverb, u ("Réverbération"), def.reverb * 100));
    layout.add (msParam (predelay, u ("Pré-délai"), 0, 120, def.predelayMs));

    layout.add (dbParam (lead, u ("Voix principale"), -60, 6, def.leadDb, true));
    layout.add (dbParam (choirLevel, u ("Chœur"), -24, 12, def.choirDb));
    layout.add (pct (width, u ("Largeur stéréo"), def.width * 100, 0, 150));
    layout.add (dbParam (gain, u ("Gain de sortie"), -24, 12, def.gainDb));

    const auto secNames = names::sectionNames();
    for (int s = 0; s < choir::kSections; ++s)
    {
        const auto& sp = def.sec[(size_t) s];
        const auto n = secNames[s] + " - ";
        layout.add (std::make_unique<APB> (pid (sec (s, on)), n + u ("actif"), sp.on));
        layout.add (std::make_unique<APC> (pid (sec (s, interval)), n + u ("intervalle"), names::intervals(), sp.interval));
        layout.add (dbParam (sec (s, level), n + u ("niveau"), -24, 6, sp.levelDb));
        layout.add (pct (sec (s, spread), n + u ("largeur"), sp.spread * 100));
        layout.add (std::make_unique<APF> (pid (sec (s, formant)), n + u ("formants"), range (-100, 100, 1.0f), sp.formant * 100,
                                           juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                               if (std::abs (v) < 0.5f) return juce::String ("Auto");
                                               return (v > 0 ? "+" : "") + juce::String (v * 0.06f, 1) + " dt";
                                           })));
        layout.add (msParam (sec (s, delay), n + u ("retard"), 0, 100, sp.delayMs));
        layout.add (std::make_unique<APF> (pid (sec (s, x)), n + u ("position X"), range (0, 1, 0.001f), sp.x));
        layout.add (std::make_unique<APF> (pid (sec (s, y)), n + u ("position Y"), range (0, 1, 0.001f), sp.y));
    }
    return layout;
}

ChoraleProcessor::ChoraleProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Entrée", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ChenevieresChorale", createLayout())
{
    using namespace ids;
    const char* globals[] = { size, mode, key, scale, source, voices, quality, fold, detune, attack, vibrato, breath, humanity,
                              hall, distance, reverb, predelay, lead, choirLevel, width, gain };
    for (size_t i = 0; i < std::size (globals); ++i) raw[i] = apvts.getRawParameterValue (globals[i]);
    for (int s = 0; s < choir::kSections; ++s)
    {
        auto g = [this, s] (const char* w) { return apvts.getRawParameterValue (sec (s, w)); };
        secRaw[s] = { g (on), g (interval), g (level), g (spread), g (formant), g (delay), g (x), g (y) };
    }
}

choir::Params ChoraleProcessor::readParams() const
{
    choir::Params p;
    auto v = [this] (int i) { return raw[i]->load (std::memory_order_relaxed); };
    auto iv = [&v] (int i) { return (int) std::lround (v (i)); };
    p.sizeIndex = iv (0);
    p.mode = (choir::HarmonyMode) iv (1);
    p.keyRoot = iv (2);
    p.scale = iv (3);
    p.sourceFemale = iv (4) == 1;
    p.numSections = iv (5) == 1 ? 4 : 3;
    p.quality = iv (6);
    p.fold = v (7) > 0.5f;
    p.detuneCents = v (8);
    p.attackMs = v (9);
    p.vibrato = v (10) * 0.01f;
    p.breath = v (11) * 0.01f;
    p.humanity = v (12) * 0.01f;
    p.hall = iv (13);
    p.distanceM = v (14);
    p.reverb = v (15) * 0.01f;
    p.predelayMs = v (16);
    p.leadDb = v (17);
    p.choirDb = v (18);
    p.width = v (19) * 0.01f;
    p.gainDb = v (20);
    for (int s = 0; s < choir::kSections; ++s)
    {
        const auto& r = secRaw[s];
        auto& sp = p.sec[(size_t) s];
        sp.on = r.on->load() > 0.5f;
        sp.interval = (int) std::lround (r.interval->load());
        sp.levelDb = r.level->load();
        sp.spread = r.spread->load() * 0.01f;
        sp.formant = r.formant->load() * 0.01f;
        sp.delayMs = r.delay->load();
        sp.x = r.x->load();
        sp.y = r.y->load();
    }
    return p;
}

bool ChoraleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    // une voix mono peut sortir en stéréo ; une sortie mono n'a qu'une entrée mono
    if (out == juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::mono()) return false;
    return true;
}

void ChoraleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (64, samplesPerBlock));
    engine.setParams (readParams());
    setLatencySamples (engine.getLatencySamples());
}

void ChoraleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = juce::jmax (1, getMainBusNumInputChannels());
    const int numOut = juce::jmax (1, getMainBusNumOutputChannels());

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }

    engine.setParams (readParams());
    const float* ins[2] = { buffer.getReadPointer (0), buffer.getReadPointer (juce::jmin (1, numIn - 1)) };
    float* outs[2] = { buffer.getWritePointer (0), buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1)) };
    engine.process (ins, juce::jmin (2, numIn), outs, juce::jmin (2, numOut), n);
}

int ChoraleProcessor::getNumPrograms() { return presets::count(); }

void ChoraleProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presets::count()) return;
    currentPreset = index;
    presets::apply (apvts, index);
}

const juce::String ChoraleProcessor::getProgramName (int index) { return presets::name (index); }

void ChoraleProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void ChoraleProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentPreset = (int) tree.getProperty ("preset", 0);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* ChoraleProcessor::createEditor() { return new ChoraleEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChoraleProcessor(); }
