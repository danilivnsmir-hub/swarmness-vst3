#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "DSP/PitchModulator.h"
#include "DSP/LivePitchShifter.h"
#include "DSP/StudioPitchShifter.h"
#include "DSP/ToneStage.h"
#include "DSP/DriveStage.h"
#include "DSP/SwarmChorus.h"
#include "DSP/FlowGate.h"
#include "Preset/PresetManager.h"

#include <array>
#include <atomic>

class SwarmnessAudioProcessor : public juce::AudioProcessor,
                                private juce::AsyncUpdater
{
public:
    SwarmnessAudioProcessor();
    ~SwarmnessAudioProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.25; }

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
        std::atomic<float> pitchSemitones { 0.0f };   // current total transposition
        std::atomic<float> flowGain { 1.0f };
    };

    Meters& getMeters() noexcept { return meters; }

    /** Editor scale factor, persisted with the plug-in state. */
    float getUiScale() const noexcept     { return uiScale.load(); }
    void setUiScale (float scale) noexcept { uiScale = scale; }

private:
    void handleAsyncUpdate() override;
    int computeLatency (int qualityIndex) const;
    void processPitch (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples);

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<PresetManager> presetManager;

    // Parameters (cached raw pointers - lock-free reads on the audio thread)
    struct Params
    {
        std::atomic<float>* pitchOn {};   std::atomic<float>* octave {};    std::atomic<float>* semitone {};
        std::atomic<float>* rise {};      std::atomic<float>* randRange {}; std::atomic<float>* randSpeed {};
        std::atomic<float>* quality {};   std::atomic<float>* rush {};      std::atomic<float>* anger {};
        std::atomic<float>* modRate {};   std::atomic<float>* lowCut {};    std::atomic<float>* highCut {};
        std::atomic<float>* mid {};       std::atomic<float>* swarmOn {};   std::atomic<float>* swarmDeep {};
        std::atomic<float>* swarmRate {}; std::atomic<float>* swarmDepth {};std::atomic<float>* swarmMix {};
        std::atomic<float>* flowOn {};    std::atomic<float>* flowHard {};  std::atomic<float>* flowSync {};
        std::atomic<float>* flowAmount {};std::atomic<float>* flowSpeed {}; std::atomic<float>* flowDiv {};
        std::atomic<float>* mix {};       std::atomic<float>* drive {};     std::atomic<float>* output {};
        std::atomic<float>* bypass {};
    } p;

    juce::AudioParameterBool* bypassParam = nullptr;

    // DSP
    PitchModulator     pitchMod;
    LivePitchShifter   liveShifter;
    StudioPitchShifter studioShifter;
    ToneStage          tone;
    DriveStage         drive;
    SwarmChorus        swarmChorus;
    FlowGate           flow;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> pitchDryDelay { 1 };

    juce::AudioBuffer<float> dryBuffer, pitchDryBuffer;

    juce::SmoothedValue<float> mixSmoothed, outputGainSmoothed, bypassSmoothed, pitchWetSmoothed;
    swarm::OnePole basePitchGlide;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;
    int activeQuality = -1;          // quality currently used by the audio thread
    int pitchLatency = 0;            // latency of the active pitch engine
    std::atomic<int> pendingLatency { 0 };
    bool wasPitchOn = true;
    float currentRiseMs = -1.0f;
    float lastRatio = 1.0f;

    Meters meters;
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessor)
};
