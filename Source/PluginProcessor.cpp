#include "PluginProcessor.h"
#include "PluginEditor.h"

SwarmnesssAudioProcessor::SwarmnesssAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        #if !JucePlugin_IsMidiEffect
         #if !JucePlugin_IsSynth
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
         #endif
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        #endif
      ),
#endif
      mAPVTS(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    mPresetManager = std::make_unique<PresetManager>(mAPVTS);

    // Cache parameter pointers
    pOctaveMode = mAPVTS.getRawParameterValue("octaveMode");
    pEngage = mAPVTS.getRawParameterValue("engage");
    pRise = mAPVTS.getRawParameterValue("rise");
    pSlideRange = mAPVTS.getRawParameterValue("slideRange");
    pSlideTime = mAPVTS.getRawParameterValue("slideTime");
    pSlideDirection = mAPVTS.getRawParameterValue("slideDirection");
    pAutoSlide = mAPVTS.getRawParameterValue("autoSlide");
    pSlidePosition = mAPVTS.getRawParameterValue("slidePosition");
    pSlideReturn = mAPVTS.getRawParameterValue("slideReturn");
    pRandomRange = mAPVTS.getRawParameterValue("randomRange");
    pRandomRate = mAPVTS.getRawParameterValue("randomRate");
    pRandomSmooth = mAPVTS.getRawParameterValue("randomSmooth");
    pRandomMode = mAPVTS.getRawParameterValue("randomMode");
    pPanic = mAPVTS.getRawParameterValue("panic");
    pChaos = mAPVTS.getRawParameterValue("chaos");
    pSpeed = mAPVTS.getRawParameterValue("speed");
    pLowCut = mAPVTS.getRawParameterValue("lowCut");
    pHighCut = mAPVTS.getRawParameterValue("highCut");
    pChorusMode = mAPVTS.getRawParameterValue("chorusMode");
    pChorusRate = mAPVTS.getRawParameterValue("chorusRate");
    pChorusDepth = mAPVTS.getRawParameterValue("chorusDepth");
    pChorusMix = mAPVTS.getRawParameterValue("chorusMix");
    pSaturation = mAPVTS.getRawParameterValue("saturation");
    pMix = mAPVTS.getRawParameterValue("mix");
    pDrive = mAPVTS.getRawParameterValue("drive");
    pOutputGain = mAPVTS.getRawParameterValue("outputGain");
    pFlowMode = mAPVTS.getRawParameterValue("flowMode");
    pFlowAmount = mAPVTS.getRawParameterValue("flowAmount");
    pFlowSpeed = mAPVTS.getRawParameterValue("flowSpeed");
    pGlobalBypass = mAPVTS.getRawParameterValue("globalBypass");
}

SwarmnesssAudioProcessor::~SwarmnesssAudioProcessor() {}

const juce::String SwarmnesssAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool SwarmnesssAudioProcessor::acceptsMidi() const { return false; }
bool SwarmnesssAudioProcessor::producesMidi() const { return false; }
bool SwarmnesssAudioProcessor::isMidiEffect() const { return false; }
double SwarmnesssAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SwarmnesssAudioProcessor::getNumPrograms() { return 1; }
int SwarmnesssAudioProcessor::getCurrentProgram() { return 0; }
void SwarmnesssAudioProcessor::setCurrentProgram(int index) {}
const juce::String SwarmnesssAudioProcessor::getProgramName(int index) { return {}; }
void SwarmnesssAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void SwarmnesssAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    mPitchShifter.prepare(sampleRate, samplesPerBlock);
    mPitchSlide.prepare(sampleRate);
    mPitchRandomizer.prepare(sampleRate);
    mModulation.prepare(sampleRate);
    mFilterEngine.prepare(spec);
    mChorusEngine.prepare(spec);
    mFlowEngine.prepare(spec);
    mDCBlocker.prepare(sampleRate);
    mSaturation.prepare(sampleRate);

    mDryBuffer.setSize(spec.numChannels, samplesPerBlock);
}

void SwarmnesssAudioProcessor::releaseResources() {
    mPitchShifter.reset();
    mPitchSlide.reset();
    mPitchRandomizer.reset();
    mModulation.reset();
    mFilterEngine.reset();
    mChorusEngine.reset();
    mFlowEngine.reset();
    mDCBlocker.reset();
    mSaturation.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SwarmnesssAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SwarmnesssAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Clear unused channels
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);

    // Global Bypass - pass input through unchanged
    if (*pGlobalBypass > 0.5f) {
        return;
    }

    // Store dry signal for final mix
    mDryBuffer.makeCopyOf(buffer, true);

    // === Update DSP parameters ===
    
    // Pitch Shifter - ALWAYS engaged for 100% wet signal
    mPitchShifter.setOctaveMode(static_cast<int>(*pOctaveMode));
    mPitchShifter.setEngage(*pEngage > 0.5f);
    mPitchShifter.setRiseTime(*pRise * 2000.0f);  // Normalize to ms
    mPitchShifter.setPanic(*pPanic);
    mPitchShifter.setChaos(*pChaos);

    // Pitch Slide
    mPitchSlide.setSlideRange(*pSlideRange * 48.0f - 24.0f);  // -24 to +24 st
    mPitchSlide.setSlideTime(50.0f + *pSlideTime * 4950.0f);  // 50-5000 ms
    mPitchSlide.setDirection(static_cast<PitchSlideEngine::Direction>(static_cast<int>(*pSlideDirection)));
    mPitchSlide.setAutoSlide(*pAutoSlide > 0.5f);
    mPitchSlide.setPosition(*pSlidePosition);
    mPitchSlide.setReturn(*pSlideReturn > 0.5f);

    // Pitch Randomizer
    mPitchRandomizer.setRandomRange(*pRandomRange * 24.0f);  // 0-24 st
    mPitchRandomizer.setRandomRate(0.1f + *pRandomRate * 9.9f);  // 0.1-10 Hz
    mPitchRandomizer.setSmooth(*pRandomSmooth);
    mPitchRandomizer.setMode(static_cast<PitchRandomizer::Mode>(static_cast<int>(*pRandomMode)));

    // Modulation
    mModulation.setLFORate(*pSpeed);
    mModulation.setLFODepth(*pPanic);
    mModulation.setRandomAmount(*pChaos);

    // Filters
    mFilterEngine.setLowCut(20.0f + *pLowCut * 480.0f);    // 20-500 Hz
    mFilterEngine.setHighCut(1000.0f + *pHighCut * 19000.0f);  // 1k-20k Hz

    // Chorus (SWARM section)
    mChorusEngine.setMode(static_cast<ChorusEngine::Mode>(static_cast<int>(*pChorusMode)));
    mChorusEngine.setRate(0.1f + *pChorusRate * 4.9f);  // 0.1-5 Hz
    mChorusEngine.setDepth(*pChorusDepth);
    mChorusEngine.setMix(*pChorusMix);

    // Saturation
    mSaturation.setDrive(*pSaturation);
    mSaturation.setMix(1.0f);

    // Flow (stutter/gate) - Improved controls
    mFlowEngine.setMode(static_cast<FlowEngine::Mode>(static_cast<int>(*pFlowMode)));
    mFlowEngine.setFlowAmount(*pFlowAmount);  // Depth of gate effect
    mFlowEngine.setPulseRate(0.5f + *pFlowSpeed * 19.5f);  // 0.5-20 Hz for stutters

    // === Process Audio ===
    
    // Calculate dynamic pitch offset from slide, randomizer, and modulation
    float totalPitchOffset = 0.0f;
    float modulationOffset = 0.0f;
    for (int sample = 0; sample < numSamples; ++sample) {
        float slideOffset = mPitchSlide.process();
        float randomOffset = mPitchRandomizer.process();
        // Get modulation value and scale it to semitones (±3 semitones max - musical glitch, not screech)
        float modValue = mModulation.getNextModulationValue();
        modulationOffset = modValue * 3.0f;  // ±3 semitones - smoother chaotic pitch modulation
        totalPitchOffset = slideOffset + randomOffset + modulationOffset;
    }
    mPitchShifter.setDynamicPitchOffset(totalPitchOffset);

    // 1. Granular Pitch Shifter (does its own wet/dry internally)
    mPitchShifter.process(buffer);

    // 2. Analog Filter Engine (HPF + LPF)
    mFilterEngine.process(buffer);

    // 3. Saturation
    if (*pSaturation > 0.01f) {
        mSaturation.process(buffer);
    }

    // 4. Chorus/SWARM modulation
    if (*pChorusMix > 0.01f) {
        mChorusEngine.process(buffer);
    }

    // 5. DC Blocker
    mDCBlocker.process(buffer);

    // 6. Flow Engine (stutter/gate) - applied AFTER other processing
    if (*pFlowAmount > 0.01f) {
        for (int sample = 0; sample < numSamples; ++sample) {
            float flowGain = mFlowEngine.process();
            for (int ch = 0; ch < numChannels; ++ch) {
                buffer.getWritePointer(ch)[sample] *= flowGain;
            }
        }
    }

    // 7. Output Drive (soft clipping)
    float drive = *pDrive;
    if (drive > 0.01f) {
        float driveAmount = 1.0f + drive * 4.0f;  // 1x to 5x gain
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                // Soft clipping with tanh
                data[i] = std::tanh(data[i] * driveAmount) / std::tanh(driveAmount);
            }
        }
    }

    // 8. Final Wet/Dry Mix
    float mix = *pMix;
    if (mix < 0.999f) {  // Only mix if not 100% wet
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = mDryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                wet[i] = dry[i] * (1.0f - mix) + wet[i] * mix;
            }
        }
    }

    // 9. Output Gain (-24 to +6 dB)
    float outputGainDb = *pOutputGain * 30.0f - 24.0f;  // Normalize to dB
    float outputGain = juce::Decibels::decibelsToGain(outputGainDb);
    buffer.applyGain(outputGain);
}

bool SwarmnesssAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SwarmnesssAudioProcessor::createEditor() {
    return new SwarmnesssAudioProcessorEditor(*this);
}

void SwarmnesssAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = mAPVTS.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SwarmnesssAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr) {
        if (xmlState->hasTagName(mAPVTS.state.getType())) {
            mAPVTS.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SwarmnesssAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // === VOLTAGE Section (Pitch controls) ===
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "octaveMode", "Octave Mode", juce::StringArray{"-2 OCT", "-1 OCT", "0", "+1 OCT", "+2 OCT"}, 3));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "engage", "Engage", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rise", "Rise", 0.0f, 1.0f, 0.05f));

    // Slide Sub-section (PULSE)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "slideRange", "Slide Range", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "slideTime", "Slide Time", 0.0f, 1.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "slideDirection", "Slide Direction", juce::StringArray{"Up", "Down", "Both"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "autoSlide", "Auto Slide", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "slidePosition", "Slide Position", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "slideReturn", "Slide Return", true));

    // Random Sub-section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "randomRange", "Random Range", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "randomRate", "Random Rate", 0.0f, 1.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "randomSmooth", "Random Smooth", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "randomMode", "Random Mode", juce::StringArray{"Jump", "Glide"}, 0));

    // === MODULATION Section ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "panic", "Panic", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chaos", "Chaos", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "speed", "Speed", 0.0f, 1.0f, 0.0f));

    // === HIVE FILTER Section ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCut", "Low Cut", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highCut", "High Cut", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "chorusMode", "Chorus Mode", juce::StringArray{"Classic", "Deep"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusRate", "Chorus Rate", 0.0f, 1.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusDepth", "Chorus Depth", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusMix", "Chorus Mix", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "saturation", "Saturation", 0.0f, 1.0f, 0.0f));

    // === OUTPUT Section ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix", 0.0f, 1.0f, 1.0f));  // Default 100% wet
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "drive", "Drive", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain", 0.0f, 1.0f, 0.8f));

    // === FLOW Section (stutter/gate) ===
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "flowMode", "Flow Mode", juce::StringArray{"Static", "Pulse"}, 1));  // Default Pulse
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flowAmount", "Flow Amount", 0.0f, 1.0f, 0.0f));  // Depth of gate effect
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flowSpeed", "Flow Speed", 0.0f, 1.0f, 0.3f));  // Rate of stutters

    // === GLOBAL ===
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "globalBypass", "Global Bypass", false));

    return {params.begin(), params.end()};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SwarmnesssAudioProcessor();
}
