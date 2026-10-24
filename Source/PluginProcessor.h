// Chènevières Chorale — processeur
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoirEngine.h"

class ChoraleProcessor : public juce::AudioProcessor
{
public:
    ChoraleProcessor();
    ~ChoraleProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chenevieres Chorale"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // pour l'interface
    float getDetectedMidi() const { return engine.getDetectedMidi(); }
    int getActiveVoices() const { return engine.getActiveVoices(); }
    int getDetectedKey() const { return engine.getDetectedKey(); }
    float getInputLevel() const { return engine.getInputLevel(); }
    float getOutputLevel() const { return engine.getOutputLevel(); }
    int currentPreset = 0;
    choir::Params currentParams() const { return readParams(); }

private:
    choir::Params readParams() const;

    choir::Engine engine;
    juce::AudioBuffer<float> scratch;

    struct SecRaw { std::atomic<float>* on, * interval, * level, * spread, * formant, * delay, * x, * y; };
    std::atomic<float>* raw[32] {};
    SecRaw secRaw[choir::kSections] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoraleProcessor)
};
