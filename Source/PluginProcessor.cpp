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
double SwarmnesssAudioProcessor::getTailLengthSeconds() const { return 0.1; }

int SwarmnesssAudioProcessor::getNumPrograms() { return 1; }
int SwarmnesssAudioProcessor::getCurrentProgram() { return 0; }
void SwarmnesssAudioProcessor::setCurrentProgram(int index) {}
const juce::String SwarmnesssAudioProcessor::getProgramName(int index) { return {}; }
void SwarmnesssAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void SwarmnesssAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    mCurrentSampleRate = sampleRate;
    
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    // Prepare original Noise Glitch DSP modules
    mPitchShifter.prepare(sampleRate, samplesPerBlock);
    mModGen.prepare(sampleRate);
    mRingModL.prepare(sampleRate);
    mRingModR.prepare(sampleRate);
    
    // Prepare DC blockers (high-pass at 20Hz)
    auto dcCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0);
    mDCBlockerL.coefficients = dcCoeffs;
    mDCBlockerR.coefficients = dcCoeffs;
    mDCBlockerL.reset();
    mDCBlockerR.reset();
    
    // Prepare smoothed values
    mMixSmoothed.reset(sampleRate, 0.02);  // 20ms smoothing
    mGainSmoothed.reset(sampleRate, 0.02);

    // Prepare additional Swarmness modules
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
    mModGen.reset();
    mRingModL.reset();
    mRingModR.reset();
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

    // Get parameter values
    const int octaveMode = static_cast<int>(*pOctaveMode);
    const bool octaveActive = *pEngage > 0.5f;
    const float riseMs = *pRise * 2000.0f;  // 0-2000ms
    const float panic = *pPanic;             // 0-1 normalized
    const float chaos = *pChaos;             // 0-1 normalized
    const float speed = *pSpeed;             // 0-1 normalized
    const float mix = *pMix;
    const float outputGainDb = *pOutputGain * 30.0f - 24.0f;  // -24 to +6 dB
    
    // Update smoothed parameters
    mMixSmoothed.setTargetValue(mix);
    mGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outputGainDb));
    
    // Update pitch shifter
    mPitchShifter.setOctaveMode(octaveMode);
    mPitchShifter.setEngage(octaveActive);
    mPitchShifter.setRiseTime(riseMs);
    
    // Update modulation generator (original Noise Glitch algorithm)
    mModGen.setParams(panic, chaos, speed);
    
    // Update ring modulators for Speed effect
    float ringFreq = 20.0f + speed * 300.0f;  // 20-320 Hz
    mRingModL.setFrequency(ringFreq);
    mRingModR.setFrequency(ringFreq);
    mRingModL.setAmount(speed);
    mRingModR.setAmount(speed);
    
    // Store dry signal
    mDryBuffer.makeCopyOf(buffer, true);
    
    // Get channel pointers
    float* channelL = buffer.getWritePointer(0);
    float* channelR = numChannels > 1 ? buffer.getWritePointer(1) : channelL;
    
    // === ORIGINAL NOISE GLITCH PROCESSING FLOW ===
    // Per-sample processing for modulation
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Get modulation values (Panic + Chaos combined pitch modulation)
        float pitchMod = mModGen.getPitchModulation();
        
        // Apply pitch modulation to shifter
        mPitchShifter.setModulation(pitchMod);
    }
    
    // Process pitch shifting (stereo)
    mPitchShifter.processStereo(channelL, channelR, numSamples);
    
    // Per-sample post-processing
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Apply ring modulation (Speed effect)
        channelL[sample] = mRingModL.processSample(channelL[sample]);
        if (numChannels > 1)
            channelR[sample] = mRingModR.processSample(channelR[sample]);
        
        // Apply DC blocking
        channelL[sample] = mDCBlockerL.processSample(channelL[sample]);
        if (numChannels > 1)
            channelR[sample] = mDCBlockerR.processSample(channelR[sample]);
        
        // Get dry samples
        float dryL = mDryBuffer.getSample(0, sample);
        float dryR = numChannels > 1 ? mDryBuffer.getSample(1, sample) : dryL;
        
        // Mix dry/wet
        float currentMix = mMixSmoothed.getNextValue();
        float currentGain = mGainSmoothed.getNextValue();
        
        channelL[sample] = (dryL * (1.0f - currentMix) + channelL[sample] * currentMix) * currentGain;
        if (numChannels > 1)
            channelR[sample] = (dryR * (1.0f - currentMix) + channelR[sample] * currentMix) * currentGain;
    }
    
    // === ADDITIONAL SWARMNESS PROCESSING ===
    
    // Filters (TONE section)
    mFilterEngine.setLowCut(20.0f + *pLowCut * 480.0f);    // 20-500 Hz
    mFilterEngine.setHighCut(1000.0f + *pHighCut * 19000.0f);  // 1k-20k Hz
    mFilterEngine.process(buffer);
    
    // Saturation (MID BOOST)
    if (*pSaturation > 0.01f) {
        mSaturation.setDrive(*pSaturation);
        mSaturation.setMix(1.0f);
        mSaturation.process(buffer);
    }
    
    // Chorus/SWARM modulation
    if (*pChorusMix > 0.01f) {
        mChorusEngine.setMode(static_cast<ChorusEngine::Mode>(static_cast<int>(*pChorusMode)));
        mChorusEngine.setRate(0.1f + *pChorusRate * 4.9f);  // 0.1-5 Hz
        mChorusEngine.setDepth(*pChorusDepth);
        mChorusEngine.setMix(*pChorusMix);
        mChorusEngine.process(buffer);
    }
    
    // Flow Engine (stutter/gate)
    if (*pFlowAmount > 0.01f) {
        mFlowEngine.setMode(static_cast<FlowEngine::Mode>(static_cast<int>(*pFlowMode)));
        mFlowEngine.setFlowAmount(*pFlowAmount);
        mFlowEngine.setPulseRate(0.5f + *pFlowSpeed * 19.5f);
        for (int sample = 0; sample < numSamples; ++sample) {
            float flowGain = mFlowEngine.process();
            for (int ch = 0; ch < numChannels; ++ch) {
                buffer.getWritePointer(ch)[sample] *= flowGain;
            }
        }
    }
    
    // Drive (soft clipping)
    float drive = *pDrive;
    if (drive > 0.01f) {
        float driveAmount = 1.0f + drive * 4.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                data[i] = std::tanh(data[i] * driveAmount) / std::tanh(driveAmount);
            }
        }
    }
    
    // Final soft clip to prevent harsh clipping (original Noise Glitch)
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channel = buffer.getWritePointer(ch);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            channel[sample] = std::tanh(channel[sample]);
        }
    }
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

    // === PITCH Section (Octave + randomizer) ===
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
