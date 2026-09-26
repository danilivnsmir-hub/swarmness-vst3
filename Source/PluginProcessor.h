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
    bool acceptsMidi() const override  { return true; }   // footswitch MIDI learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }   // TRAILS up to 2 s x high feedback

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

public:
    //==============================================================================
    /** MIDI learn for the footswitches: 0 = +1 OCT, 1 = +2 OCT, 2 = VENOM, 3 = ON. */
    static constexpr int kNumMidiTargets = 4;
    enum class MidiKind : int { none = 0, cc = 1, note = 2 };

    void startMidiLearn (int target) noexcept     { midiLearnTarget.store (target); }
    void cancelMidiLearn() noexcept               { midiLearnTarget.store (-1); }
    void clearMidiBinding (int target) noexcept   { midiMap[(size_t) target].kind.store (0); }
    int  getMidiLearnTarget() const noexcept      { return midiLearnTarget.load(); }
    juce::String describeMidiBinding (int target) const;

private:
    struct MidiBinding { std::atomic<int> kind { 0 }, number { -1 }; std::atomic<bool> down { false }; };
    std::array<MidiBinding, kNumMidiTargets> midiMap;
    std::atomic<int> midiLearnTarget { -1 };
    void handleMidi (const juce::MidiBuffer&);
    void applyFootswitch (int target, bool pressed);

    // Parameters (cached raw pointers - lock-free reads on the audio thread)
    struct Params
    {
        std::atomic<float>* oct1 {};       std::atomic<float>* oct2 {};        std::atomic<float>* noiseDown {};
        std::atomic<float>* rise {};       std::atomic<float>* panic {};       std::atomic<float>* chaos {};
        std::atomic<float>* speed {};      std::atomic<float>* fall {};        std::atomic<float>* stingMix {};    std::atomic<float>* stingRaw {};    std::atomic<float>* stingDetune {}; std::atomic<float>* rbDetune {};    std::atomic<float>* rbRaw {};
        std::atomic<float>* rbOn {};       std::atomic<float>* rbPitch {};     std::atomic<float>* rbSnap {};
        std::atomic<float>* rbPrimary {};  std::atomic<float>* rbSecondary {}; std::atomic<float>* rbTone {};
        std::atomic<float>* rbTracking {}; std::atomic<float>* rbMagic {};     std::atomic<float>* magicHold {};
        std::atomic<float>* linkOct1 {};   std::atomic<float>* linkOct2 {};
        std::atomic<float>* rbMix {};      std::atomic<float>* rbTime {};     std::atomic<float>* rbSync {};      std::atomic<float>* rbDiv {};
        std::atomic<float>* swarmOn {};    std::atomic<float>* swarmDeep {};   std::atomic<float>* swarmRate {};
        std::atomic<float>* swarmDepth {}; std::atomic<float>* swarmMix {};
        std::atomic<float>* fuzzOn {};     std::atomic<float>* fuzzPost {};    std::atomic<float>* fuzz {};
        std::atomic<float>* fuzzTone {};   std::atomic<float>* fuzzGate {};    std::atomic<float>* fuzzVoice {};
        std::atomic<float>* fuzzScoop {};  std::atomic<float>* fuzzGlare {};   std::atomic<float>* fuzzBlend {};   std::atomic<float>* fuzzSag {};
        std::atomic<float>* flowOn {};     std::atomic<float>* flowHard {};    std::atomic<float>* flowSync {};
        std::atomic<float>* flowAmount {}; std::atomic<float>* flowSpeed {};   std::atomic<float>* flowDiv {};
        std::atomic<float>* output {};      std::atomic<float>* input {};      std::atomic<float>* bypass {};
    } p;

    juce::AudioParameterBool* bypassParam = nullptr;

    // DSP chain: [fuzz pre] -> (noise || rainbow voices) -> swarm -> [fuzz post] -> flow (each stage has its own blend)
    FuzzStage    fuzzPre, fuzzPost;
    NoiseStage   noise;
    RainbowStage rainbow;
    SwarmChorus  swarmChorus;
    FlowGate     flow;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer, hiveBuffer;
    std::vector<float> inputGainTrack;     // per-sample INPUT gain of the current block
    juce::SmoothedValue<float> inputGainSmoothed, outputGainSmoothed, hiveVoiceGain, bypassSmoothed;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    Meters meters;
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessor)
};
