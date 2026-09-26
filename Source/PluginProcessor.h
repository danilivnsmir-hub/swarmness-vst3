#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "DSP/NoiseStage.h"
#include "DSP/RainbowStage.h"
#include "DSP/FuzzStage.h"
#include "DSP/SwarmChorus.h"
#include "DSP/FlowGate.h"
#include "DSP/Equalisers.h"
#include "DSP/ReverbStage.h"
#include "DSP/SpectrumTap.h"
#include "Preset/PresetManager.h"

#include <array>
#include <atomic>
#include <optional>

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
    double getTailLengthSeconds() const override { return 20.0; }   // CRYPT decay up to 20 s, TRAILS up to ~10 s

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
        std::atomic<float> reverbLevel { 0.0f };       // CRYPT wet peak
    };

    Meters& getMeters() noexcept { return meters; }

    /** Output tap for the EQ analyser (the editor switches it on while it is visible). */
    SpectrumTap& getSpectrumTap() noexcept { return spectrumTap; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    /** The chain order / routing the parameters ask for (message or audio thread). */
    Chain::Layout getRequestedLayout() const noexcept;
    Chain::Order getRequestedChainOrder() const noexcept { return getRequestedLayout().order; }
    /** Writes the slot and lane parameters for a new layout (message thread). */
    void setChainLayout (const Chain::Layout&);
    /** New order, lanes unchanged. */
    void setChainOrder (const Chain::Order&);

    /** CRYPT impulse response (message thread). Returns an error message, empty on success. */
    juce::String loadReverbIR (const juce::File&);
    void clearReverbIR();
    juce::File getReverbIRFile() const;
    juce::String getReverbIRDescription() const;   // "name - 2.4 s, stereo" (empty = none)
    std::vector<float> getReverbIREnvelope() const; // peak envelope (0..1) for the editor, empty = none
    double getReverbIRSeconds() const;

    /** Editor scale factor, persisted with the plug-in state. */
    float getUiScale() const noexcept     { return uiScale.load(); }
    void setUiScale (float scale) noexcept { uiScale = scale; }
    /** Last page shown in the editor (FX / EQ / CRYPT), kept while the plug-in is loaded. */
    int getUiPage() const noexcept         { return uiPage.load(); }
    void setUiPage (int page) noexcept     { uiPage = page; }

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
        std::atomic<float>* fuzzOn {};     std::atomic<float>* fuzz {};
        std::atomic<float>* fuzzTone {};   std::atomic<float>* fuzzGate {};    std::atomic<float>* fuzzVoice {};
        std::atomic<float>* fuzzScoop {};  std::atomic<float>* fuzzGlare {};   std::atomic<float>* fuzzBlend {};   std::atomic<float>* fuzzSag {};
        std::atomic<float>* flowOn {};     std::atomic<float>* flowHard {};    std::atomic<float>* flowSync {};
        std::atomic<float>* flowAmount {}; std::atomic<float>* flowSpeed {};   std::atomic<float>* flowDiv {};
        std::atomic<float>* output {};      std::atomic<float>* input {};      std::atomic<float>* bypass {};

        std::atomic<float>* geqOn {};      std::atomic<float>* geqLevel {};
        std::array<std::atomic<float>*, swarm::GraphicEq::numBands> geqBands {};
        std::atomic<float>* peqOn {};      std::atomic<float>* peqHpFreq {};   std::atomic<float>* peqLpFreq {};
        std::atomic<float>* peqLowFreq {}; std::atomic<float>* peqLowGain {};  std::atomic<float>* peqHighFreq {}; std::atomic<float>* peqHighGain {};
        std::array<std::atomic<float>*, 3> peqBellFreq {}, peqBellGain {}, peqBellQ {};
        std::atomic<float>* revOn {};      std::atomic<float>* revType {};     std::atomic<float>* revMix {};
        std::atomic<float>* revDecay {};   std::atomic<float>* revSize {};     std::atomic<float>* revPreDelay {};
        std::atomic<float>* revTone {};    std::atomic<float>* revLowCut {};   std::atomic<float>* revMod {};     std::atomic<float>* revDuck {};
        std::array<std::atomic<float>*, Chain::numBlocks> chainSlots {}, chainLanes {};
        std::atomic<float>* parMix {};
    } p;

    /** Per-block state shared by the chain blocks. */
    struct BlockContext
    {
        double bpm = 120.0;
        std::optional<double> ppq;
        bool magicHeld = false, oct1Held = false, oct2Held = false;
    };

    void processChainBlock (int block, const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;
    void processPitch (const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;
    void processSmoke (float* const* audio, int numChannels, int numSamples) noexcept;
    void processWings (const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;

    juce::AudioParameterBool* bypassParam = nullptr;

    // The blocks (each once; the order comes from the chain slot parameters)
    FuzzStage          fuzzStage;
    NoiseStage         noise;
    RainbowStage       rainbow;
    SwarmChorus        swarmChorus;
    FlowGate           flow;
    swarm::GraphicEq    comb;
    swarm::ParametricEq carve;
    ReverbStage        crypt;

    Chain::Layout activeLayout { Chain::defaultOrder(), {} };
    juce::SmoothedValue<float> chainFade;   // dips the chain output while the order / routing changes
    juce::SmoothedValue<float> parMixSmoothed;
    juce::AudioBuffer<float> pathBBuffer;   // parallel path B
    // Parallel paths are latency-aligned: the path without SMOKE is delayed by SMOKE's latency.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> pathAlignA { 1 }, pathAlignB { 1 };

    SpectrumTap spectrumTap;
    juce::File reverbIRFile;
    juce::String reverbIRDescription;
    std::vector<float> reverbIREnvelope;
    double reverbIRSeconds = 0.0;
    juce::CriticalSection irInfoLock;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer, hiveBuffer;
    std::vector<float> inputGainTrack;     // per-sample INPUT gain of the current block
    juce::SmoothedValue<float> inputGainSmoothed, outputGainSmoothed, hiveVoiceGain, bypassSmoothed;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    Meters meters;
    std::atomic<float> uiScale { 1.0f };
    std::atomic<int> uiPage { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessor)
};
