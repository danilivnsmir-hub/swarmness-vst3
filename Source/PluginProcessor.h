#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "DSP/HiveBlock.h"
#include "DSP/ShiftBlock.h"
#include "DSP/FuzzStage.h"
#include "DSP/SwarmChorus.h"
#include "DSP/FlowGate.h"
#include "DSP/Equalisers.h"
#include "DSP/ReverbStage.h"
#include "DSP/AmpBlock.h"
#include "DSP/CabBlock.h"
#include "DSP/DriveBlock.h"
#include "DSP/Tuner.h"
#include "DSP/SpectrumTap.h"
#include "Preset/PresetManager.h"
#include "Amp/Tone3000.h"

#include <array>
#include <atomic>
#include <optional>

class SwarmnessAudioProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
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
    /** The host calls this while ON is off: the plug-in does its own bypass (latency-aligned dry, and
        the footswitches / MIDI pedals keep working), so it takes the normal path. */
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override { processBlock (buffer, midi); }

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }

    /** Metering / visualisation data for the editor (lock-free). */
    struct Meters
    {
        std::array<std::atomic<float>, 2> input  {};
        std::array<std::atomic<float>, 2> output {};
        std::atomic<float> pitchSemitones { 0.0f };   // current SHIFT transposition
        std::atomic<bool>  noiseEngaged { false };
        std::atomic<float> stackSemitones { 0.0f };   // SHIFT STACK voice
        std::atomic<bool>  stackOn { false };
        std::atomic<int>   trailStep { -1 };             // TRAILS step now playing (-1 = none)
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

    /** AMP: a Neural Amp Modeler capture (.nam), message thread. Returns an error message, empty on success. */
    // NAM captures: the AMP's (pedal = false) and WASP's pedal capture (pedal = true)
    juce::String loadNamModel (const juce::File&, bool pedal = false);
    void clearNamModel (bool pedal = false);
    juce::File getNamModelFile (bool pedal = false) const;
    juce::String getNamModelDescription (bool pedal = false) const;   // "name - WaveNet, 48 kHz" (empty = none)

    /** CAB: a loaded cabinet impulse response (message thread). */
    // CAB IRs: slot A (0) and slot B (1), mixed with CAB IR MIX
    juce::String loadCabIR (const juce::File&, int slot = 0);
    void clearCabIR (int slot = 0);
    juce::File getCabIRFile (int slot = 0) const;
    juce::String getCabIRDescription (int slot = 0) const;
    /** TONE3000: pick a NAM capture (AMP) or a cabinet IR (CAB) on tone3000.com; it downloads and loads. */
    void browseTone3000 (Tone3000::Target, Tone3000::Architecture = Tone3000::Architecture::a1, int cabSlot = 0);
    /** TONE3000 takes one NAM architecture per flow: the one picked last (the menu ticks it). */
    Tone3000::Architecture lastTone3000Architecture = Tone3000::Architecture::a2;

    /** TUNER: the editor opens it (active) and reads the guitar from it; MUTE silences the output while it is open. */
    TunerTap& getTuner() noexcept { return tunerTap; }
    int tone3000CabSlot = 0;   // the IR slot a TONE3000 cabinet goes to
    juce::String getTone3000Status() const { return tone3000.getStatus(); }
    /** The previous / next capture (or IR) in the folder of the loaded one - e.g. the other models of a TONE3000 tone. */
    void stepNamModel (int direction, bool pedal = false);
    void stepCabIR (int direction, int slot = 0);

    /** Rebuilds the modelled cabinet IR when its settings changed (message thread; also run by a timer). */
    void updateCabModel (bool force = false);

    /** Editor scale factor, persisted with the plug-in state. */
    float getUiScale() const noexcept     { return uiScale.load(); }
    void setUiScale (float scale) noexcept { uiScale = scale; }
    bool getUiMini() const noexcept       { return uiMini.load(); }
    void setUiMini (bool m) noexcept      { uiMini = m; }
    /** Last page shown in the editor (FX / EQ / CRYPT), kept while the plug-in is loaded. */
    int getUiPage() const noexcept         { return uiPage.load(); }
    void setUiPage (int page) noexcept     { uiPage = page; }

private:
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<PresetManager> presetManager;

public:
    //==============================================================================
    /**
     * MIDI learn for any parameter (right-click a control). One CC / note can drive several
     * parameters, and a parameter can have several bindings.
     *  - on / off switches toggle on every press (CC >= 64 or note-on); the SHIFT A / B and VENOM
     *    footswitches follow MOMENTARY / LATCH (held = on)
     *  - choices step to the next option on every press
     *  - knobs follow the CC value
     */
    static constexpr int kMaxMidiBindings = 64;
    enum class MidiKind : int { none = 0, cc = 1, note = 2 };

    /** value >= 0: the binding sets that value on every press (e.g. one scene button). */
    void startMidiLearn (const juce::String& paramID, int value = -1) noexcept;
    void cancelMidiLearn() noexcept               { midiLearnParam.store (-1); }
    void clearMidiBindings (const juce::String& paramID, int value = -1) noexcept;
    /** The parameter waiting for a MIDI message, empty when not learning. */
    juce::String getMidiLearnParam() const;
    int getMidiLearnValue() const noexcept        { return midiLearnValue.load(); }
    /** "CC 64, Note C1" - empty when not assigned. */
    juce::String describeMidiBinding (const juce::String& paramID, int value = -1) const;

private:
    struct MidiBinding { std::atomic<int> param { -1 }, kind { 0 }, number { -1 }, value { -1 }; std::atomic<bool> down { false }; };
    std::array<MidiBinding, kMaxMidiBindings> midiBindings;
    std::atomic<int> midiLearnParam { -1 }, midiLearnValue { -1 };

    // Scenes: the scene parameter (host automation, MIDI, the scene buttons) switches the preset's
    // scene on the message thread
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    /** Background upkeep (not a juce::Timer: the host may destroy the plug-in on another thread, and
        a Timer callback still running on the message thread would then touch a dying processor).
        The destructor stops and joins it before anything else goes. */
    struct Housekeeper : juce::Thread
    {
        explicit Housekeeper (SwarmnessAudioProcessor& p) : juce::Thread ("Swarmness housekeeping"), owner (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                owner.updateCabModel();   // modelled cabinet IR rebuilds
                owner.amp.releaseRetired();   // frees NAM captures the audio thread swapped out
                owner.amp.setNamSize (owner.p.namLite->load() > 0.5f ? 0.0 : 1.0);   // A2 Full / Lite
                owner.wasp.releaseRetired();
                owner.cab.setUserMix (owner.p.cabIrMix->load() * 0.01f, owner.p.cabIrInvB->load() > 0.5f);
                owner.wasp.setNamSize (owner.p.drvNamLite->load() > 0.5f ? 0.0 : 1.0);
                wait (100);
            }
        }
        SwarmnessAudioProcessor& owner;
    };
    std::unique_ptr<Housekeeper> housekeeper;
    Tone3000 tone3000;
    TunerTap tunerTap;
    juce::SmoothedValue<float> tunerGain;
    std::atomic<int> pendingScene { 0 };
    bool restoringState = false;
    juce::Array<juce::RangedAudioParameter*> learnableParams;   // index = binding param
    int indexOfParam (const juce::String& paramID) const noexcept;
    void handleMidi (const juce::MidiBuffer&);
    void applyMidi (juce::RangedAudioParameter&, bool isCC, int ccValue, bool pressed);


    // Parameters (cached raw pointers - lock-free reads on the audio thread)
    struct Params
    {
        std::atomic<float>* oct1 {};       std::atomic<float>* oct2 {};        std::atomic<float>* shiftA {};    std::atomic<float>* shiftB {};
        std::atomic<float>* trSteps {};    std::atomic<float>* trChop {};     std::atomic<float>* trDry {};      std::atomic<float>* shOn {};
        std::array<std::atomic<float>*, 16> trLevels {}, trMoves {};
        std::atomic<float>* rise {};       std::atomic<float>* panic {};       std::atomic<float>* chaos {};
        std::atomic<float>* speed {};      std::atomic<float>* fall {};        std::atomic<float>* stingMix {};    std::atomic<float>* rbDetune {};    std::atomic<float>* rbRaw {};
        std::atomic<float>* shStack {};    std::atomic<float>* shSnap {};      std::atomic<float>* shRaw {};       std::atomic<float>* shDetune {};
        std::atomic<float>* hvMangle {};
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
        std::atomic<float>* ampOn {};      std::atomic<float>* ampChannel {};  std::atomic<float>* ampGain {};
        std::atomic<float>* ampBass {};    std::atomic<float>* ampMid {};      std::atomic<float>* ampTreble {};   std::atomic<float>* ampPresence {};
        std::atomic<float>* ampDepth {};   std::atomic<float>* ampMaster {};   std::atomic<float>* ampGate {};     std::atomic<float>* ampLevel {};
        std::atomic<float>* namInput {};   std::atomic<float>* namBass {};     std::atomic<float>* namMid {};      std::atomic<float>* namTreble {};
        std::atomic<float>* namPresence {}; std::atomic<float>* namDepth {};   std::atomic<float>* namOutput {};
        std::atomic<float>* namLite {};
        std::atomic<float>* drvOn {};      std::atomic<float>* drvVolume {};   std::atomic<float>* drvDrive {};    std::atomic<float>* drvBright {};
        std::atomic<float>* drvAttack {};  std::atomic<float>* drvGate {};
        std::atomic<float>* drvNam {};     std::atomic<float>* drvNamInput {}; std::atomic<float>* drvNamOutput {}; std::atomic<float>* drvNamLite {};
        std::atomic<float>* cabOn {};      std::atomic<float>* cabType {};     std::atomic<float>* cabMic {};      std::atomic<float>* cabDist {};
        std::atomic<float>* cabLowCut {};  std::atomic<float>* cabHighCut {};  std::atomic<float>* cabLevel {};
        std::atomic<float>* cabIrMix {};   std::atomic<float>* cabIrInvB {};
        std::array<std::atomic<float>*, Chain::numBlocks> chainSlots {}, chainLanes {};
        std::array<std::atomic<float>*, Chain::maxSplits> parMix {};
    } p;

    /** Per-block state shared by the chain blocks. */
    struct BlockContext
    {
        double bpm = 120.0;
        std::optional<double> ppq;
        bool magicHeld = false, oct1Held = false, oct2Held = false;
    };

    void processChainBlock (int block, const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;
    void processShift (const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;
    void processHive (const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;
    void processSmoke (float* const* audio, int numChannels, int numSamples) noexcept;
    void processWings (const BlockContext&, float* const* audio, int numChannels, int numSamples) noexcept;

    juce::AudioParameterBool* bypassParam = nullptr;

    // The blocks (each once; the order comes from the chain slot parameters)
    FuzzStage          fuzzStage;
    ShiftBlock         shift;         // footswitch shifter
    HiveBlock          hive;          // VOICES + TRAILS
    SwarmChorus        swarmChorus;
    FlowGate           flow;
    swarm::GraphicEq    comb;
    swarm::ParametricEq carve;
    ReverbStage        crypt;
    AmpBlock           amp;
    CabBlock           cab;
    DriveBlock         wasp;

    Chain::Layout activeLayout { Chain::defaultOrder(), {} };
    juce::SmoothedValue<float> chainFade;   // dips the chain output while the order / routing changes
    std::array<juce::SmoothedValue<float>, Chain::maxSplits> parMixSmoothed;
    juce::AudioBuffer<float> pathBBuffer;   // parallel path B
    // Parallel paths are latency-aligned: the path without SMOKE is delayed by SMOKE's latency.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> pathAlignA { 1 }, pathAlignB { 1 };

    SpectrumTap spectrumTap;
    juce::File reverbIRFile;
    juce::String reverbIRDescription;
    std::vector<float> reverbIREnvelope;
    double reverbIRSeconds = 0.0;
    juce::CriticalSection irInfoLock;
    std::array<juce::File, 2> namFile;
    std::array<juce::File, 2> cabIRFile;
    std::array<juce::String, 2> namDescription;
    std::array<juce::String, 2> cabIRDescription;
    int cabModelKey = -1;   // type / mic / distance / rate the modelled cabinet IR was built for
    double cabModelRate = 0.0;
    juce::CriticalSection cabModelLock;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;
    std::vector<float> inputGainTrack;     // per-sample INPUT gain of the current block
    juce::SmoothedValue<float> inputGainSmoothed, outputGainSmoothed, bypassSmoothed;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    Meters meters;
    std::atomic<float> uiScale { 1.0f };
    std::atomic<bool> uiMini { false };
    std::atomic<int> uiPage { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessor)
};
