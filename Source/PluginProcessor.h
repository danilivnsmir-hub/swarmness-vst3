#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "DSP/NoiseStage.h"
#include "DSP/RainbowStage.h"
#include "DSP/FuzzStage.h"
#include "DSP/SwarmChorus.h"
#include "DSP/FlowGate.h"
#include "Preset/PresetManager.h"

#include <array>
#include <atomic>

class SwarmnessAudioProcessor : public juce::AudioProcessor
{
public:
    SwarmnessAudioProcessor();
    ~SwarmnessAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }

    /** Metering / visualisation data for the editor (lock-free). */
    struct Meters
    {
        std::array<std::atomic<float>, 2> input  {};
        std::array<std::atomic<float>, 2> output {};
        std::atomic<float> pitchSemitones { 0.0f };   // current NOISE transposition
        std::atomic<bool>  noiseEngaged { false };
    };

    Meters& getMeters() noexcept { return meters; }

    /** Editor scale factor, persisted with the plug-in state. */
    float getUiScale() const noexcept     { return uiScale.load(); }
    void setUiScale (float scale) noexcept { uiScale = scale; }

private:
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<PresetManager> presetManager;

    // Parameters (cached raw pointers - lock-free reads on the audio thread)
    struct Params
    {
        std::atomic<float>* oct1 {};       std::atomic<float>* oct2 {};        std::atomic<float>* noiseDown {};
        std::atomic<float>* rise {};       std::atomic<float>* panic {};       std::atomic<float>* chaos {};
        std::atomic<float>* speed {};      std::atomic<float>* fall {};
        std::atomic<float>* rbOn {};       std::atomic<float>* rbPitch {};     std::atomic<float>* rbSnap {};
        std::atomic<float>* rbPrimary {};  std::atomic<float>* rbSecondary {}; std::atomic<float>* rbTone {};
        std::atomic<float>* rbTracking {}; std::atomic<float>* rbMagic {};     std::atomic<float>* magicHold {};
        std::atomic<float>* linkOct1 {};   std::atomic<float>* linkOct2 {};
        std::atomic<float>* swarmOn {};    std::atomic<float>* swarmDeep {};   std::atomic<float>* swarmRate {};
        std::atomic<float>* swarmDepth {}; std::atomic<float>* swarmMix {};
        std::atomic<float>* fuzzOn {};     std::atomic<float>* fuzzPost {};    std::atomic<float>* fuzz {};
        std::atomic<float>* fuzzTone {};   std::atomic<float>* fuzzGate {};
        std::atomic<float>* flowOn {};     std::atomic<float>* flowHard {};    std::atomic<float>* flowSync {};
        std::atomic<float>* flowAmount {}; std::atomic<float>* flowSpeed {};   std::atomic<float>* flowDiv {};
        std::atomic<float>* mix {};        std::atomic<float>* output {};      std::atomic<float>* bypass {};
    } p;

    juce::AudioParameterBool* bypassParam = nullptr;

    // DSP chain: [fuzz pre] -> noise -> rainbow -> swarm -> [fuzz post] -> mix -> flow
    FuzzStage    fuzzPre, fuzzPost;
    NoiseStage   noise;
    RainbowStage rainbow;
    SwarmChorus  swarmChorus;
    FlowGate     flow;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> mixSmoothed, outputGainSmoothed, bypassSmoothed;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    Meters meters;
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessor)
};
