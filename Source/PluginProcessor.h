#pragma once
#include <JuceHeader.h>
#include "DSP/GranularPitchShifter.h"
#include "DSP/PitchSlideEngine.h"
#include "DSP/PitchRandomizer.h"
#include "DSP/Modulation.h"
#include "DSP/AnalogFilterEngine.h"
#include "DSP/ChorusEngine.h"
#include "DSP/FlowEngine.h"
#include "DSP/DCBlocker.h"
#include "DSP/Saturation.h"
#include "Preset/PresetManager.h"

class SwarmnesssAudioProcessor : public juce::AudioProcessor {
public:
    SwarmnesssAudioProcessor();
    ~SwarmnesssAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return mAPVTS; }
    PresetManager& getPresetManager() { return *mPresetManager; }
    FlowEngine& getFlowEngine() { return mFlowEngine; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState mAPVTS;
    std::unique_ptr<PresetManager> mPresetManager;

    // DSP Modules
    GranularPitchShifter mPitchShifter;
    PitchSlideEngine mPitchSlide;
    PitchRandomizer mPitchRandomizer;
    Modulation mModulation;
    AnalogFilterEngine mFilterEngine;
    ChorusEngine mChorusEngine;
    FlowEngine mFlowEngine;
    DCBlocker mDCBlocker;
    Saturation mSaturation;

    // Parameter pointers
    std::atomic<float>* pOctaveMode = nullptr;
    std::atomic<float>* pEngage = nullptr;
    std::atomic<float>* pRise = nullptr;
    std::atomic<float>* pSlideRange = nullptr;
    std::atomic<float>* pSlideTime = nullptr;
    std::atomic<float>* pSlideDirection = nullptr;
    std::atomic<float>* pAutoSlide = nullptr;
    std::atomic<float>* pSlidePosition = nullptr;
    std::atomic<float>* pSlideReturn = nullptr;
    std::atomic<float>* pRandomRange = nullptr;
    std::atomic<float>* pRandomRate = nullptr;
    std::atomic<float>* pRandomSmooth = nullptr;
    std::atomic<float>* pRandomMode = nullptr;
    std::atomic<float>* pPanic = nullptr;
    std::atomic<float>* pChaos = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pHighCut = nullptr;
    std::atomic<float>* pChorusMode = nullptr;
    std::atomic<float>* pChorusRate = nullptr;
    std::atomic<float>* pChorusDepth = nullptr;
    std::atomic<float>* pChorusMix = nullptr;
    std::atomic<float>* pSaturation = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pOutputGain = nullptr;
    std::atomic<float>* pFlowMode = nullptr;
    std::atomic<float>* pPulseRate = nullptr;
    std::atomic<float>* pPulseProbability = nullptr;
    std::atomic<float>* pGlobalBypass = nullptr;

    juce::AudioBuffer<float> mDryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessor)
};
