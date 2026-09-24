#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kControlBlock = 32;   // samples per pitch-modulation update

    inline void updatePeak (std::atomic<float>& meter, float value) noexcept
    {
        auto prev = meter.load (std::memory_order_relaxed);
        while (value > prev && ! meter.compare_exchange_weak (prev, value, std::memory_order_relaxed)) {}
    }
}

//==============================================================================
SwarmnessAudioProcessor::SwarmnessAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    auto get = [this] (const char* id)
    {
        auto* v = apvts.getRawParameterValue (id);
        jassert (v != nullptr);
        return v;
    };

    using namespace ParamIDs;
    p.pitchOn = get (pitchOn);     p.octave = get (octave);        p.semitone = get (semitone);
    p.rise = get (rise);           p.randRange = get (randRange);  p.randSpeed = get (randSpeed);
    p.quality = get (quality);     p.rush = get (rush);            p.anger = get (anger);
    p.modRate = get (modRate);     p.lowCut = get (lowCut);        p.highCut = get (highCut);
    p.mid = get (mid);             p.swarmOn = get (swarmOn);      p.swarmDeep = get (swarmDeep);
    p.swarmRate = get (swarmRate); p.swarmDepth = get (swarmDepth);p.swarmMix = get (swarmMix);
    p.flowOn = get (flowOn);       p.flowHard = get (flowHard);    p.flowSync = get (flowSync);
    p.flowAmount = get (flowAmount); p.flowSpeed = get (flowSpeed); p.flowDiv = get (flowDiv);
    p.mix = get (mix);             p.drive = get (ParamIDs::drive);         p.output = get (output);
    p.bypass = get (bypass);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (bypass));
    jassert (bypassParam != nullptr);

    presetManager = std::make_unique<PresetManager> (apvts);
}

SwarmnessAudioProcessor::~SwarmnessAudioProcessor()
{
    cancelPendingUpdate();
}

//==============================================================================
bool SwarmnessAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

int SwarmnessAudioProcessor::computeLatency (int qualityIndex) const
{
    const int pitch = qualityIndex == 1 ? studioShifter.getLatencySamples() : 0;
    return pitch + drive.getLatencySamples();
}

void SwarmnessAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    pitchMod.prepare (sampleRate);
    liveShifter.prepare (sampleRate, 2);
    studioShifter.prepare (sampleRate, juce::jmax (maxBlockSize, kControlBlock), 2);
    tone.prepare (sampleRate);
    drive.prepare (sampleRate, maxBlockSize);
    swarmChorus.prepare (sampleRate);
    flow.prepare (sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    const int maxLatency = studioShifter.getLatencySamples() + drive.getLatencySamples() + 64;
    dryDelay.setMaximumDelayInSamples (maxLatency);
    dryDelay.prepare (spec);
    pitchDryDelay.setMaximumDelayInSamples (maxLatency);
    pitchDryDelay.prepare (spec);

    dryBuffer.setSize (2, maxBlockSize, false, false, true);
    pitchDryBuffer.setSize (2, maxBlockSize, false, false, true);

    auto initSmoothed = [sampleRate] (juce::SmoothedValue<float>& s, double seconds, float value)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };

    initSmoothed (mixSmoothed,        0.03, p.mix->load() * 0.01f);
    initSmoothed (outputGainSmoothed, 0.03, juce::Decibels::decibelsToGain (p.output->load()));
    initSmoothed (bypassSmoothed,     0.02, p.bypass->load() > 0.5f ? 1.0f : 0.0f);
    initSmoothed (pitchWetSmoothed,   0.02, p.pitchOn->load() > 0.5f ? 1.0f : 0.0f);

    wasPitchOn = p.pitchOn->load() > 0.5f;
    currentRiseMs = -1.0f;
    const float startSemis = wasPitchOn ? (float) (ParamChoices::octaveIndexToSemitones ((int) p.octave->load())
                                                   + (int) p.semitone->load())
                                        : 0.0f;
    basePitchGlide.reset (startSemis);
    lastRatio = std::pow (2.0f, startSemis / 12.0f);

    activeQuality = (int) p.quality->load();
    pitchLatency = activeQuality == 1 ? studioShifter.getLatencySamples() : 0;
    const int total = computeLatency (activeQuality);
    dryDelay.setDelay ((float) total);
    pitchDryDelay.setDelay ((float) pitchLatency);
    pendingLatency = total;
    setLatencySamples (total);

    for (auto& m : meters.input)  m = 0.0f;
    for (auto& m : meters.output) m = 0.0f;
}

void SwarmnessAudioProcessor::releaseResources()
{
    liveShifter.reset();
    studioShifter.reset();
    tone.reset();
    drive.reset();
    swarmChorus.reset();
    dryDelay.reset();
    pitchDryDelay.reset();
}

void SwarmnessAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (pendingLatency.load());
}

//==============================================================================
void SwarmnessAudioProcessor::processPitch (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    const bool pitchOn = p.pitchOn->load() > 0.5f;

    // Engaging the section makes the pitch "rise" from unison to the target interval.
    if (pitchOn && ! wasPitchOn)
        basePitchGlide.reset (0.0f);
    wasPitchOn = pitchOn;

    const float riseMs = p.rise->load();
    if (! juce::exactlyEqual (riseMs, currentRiseMs))
    {
        currentRiseMs = riseMs;
        // One-pole reaching ~95% of the interval within the Rise time.
        basePitchGlide.setTime (currentSampleRate / kControlBlock, juce::jmax (0.0005, riseMs * 0.001 / 3.0));
    }

    const float targetSemis = pitchOn ? (float) (ParamChoices::octaveIndexToSemitones ((int) p.octave->load())
                                                 + (int) p.semitone->load())
                                      : 0.0f;

    pitchMod.setRandom (p.randRange->load(), p.randSpeed->load());
    pitchMod.setModulation (p.rush->load() * 0.01f, p.anger->load() * 0.01f, p.modRate->load());
    pitchWetSmoothed.setTargetValue (pitchOn ? 1.0f : 0.0f);

    // Section-bypass path: dry signal aligned with the pitch engine's latency.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        float* dst = pitchDryBuffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            pitchDryDelay.pushSample (ch, src[i]);
            dst[i] = pitchDryDelay.popSample (ch);
        }
    }

    float* chans[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numChannels > 1 ? 1 : 0) };
    float semis = 0.0f;

    for (int start = 0; start < numSamples; start += kControlBlock)
    {
        const int n = juce::jmin (kControlBlock, numSamples - start);

        const float base = basePitchGlide.process (targetSemis);
        const float mod  = pitchOn ? pitchMod.advance (n) : 0.0f;
        semis = juce::jlimit (-36.0f, 36.0f, base + mod);
        const float ratio = std::pow (2.0f, semis / 12.0f);

        float* sub[2] = { chans[0] + start, chans[1] + start };

        if (activeQuality == 1)
            studioShifter.process (sub, numChannels, n, ratio);
        else
            liveShifter.process (sub, numChannels, n, lastRatio, ratio);

        lastRatio = ratio;
    }

    meters.pitchSemitones.store (pitchOn ? semis : 0.0f, std::memory_order_relaxed);

    // Crossfade between shifted and aligned-dry when the section is toggled.
    for (int i = 0; i < numSamples; ++i)
    {
        const float w = pitchWetSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float dry = pitchDryBuffer.getSample (ch, i);
            d[i] = dry + w * (d[i] - dry);
        }
    }
}

void SwarmnessAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples == 0 || numChannels == 0)
        return;

    // Hosts may occasionally exceed the announced block size: process in safe chunks.
    if (numSamples > maxBlockSize)
    {
        for (int start = 0; start < numSamples; start += maxBlockSize)
        {
            const int n = juce::jmin (maxBlockSize, numSamples - start);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
            juce::MidiBuffer dummy;
            processBlock (sub, dummy);
        }
        return;
    }

    // ---- Quality (pitch engine) change: swap engines and update reported latency
    const int quality = (int) p.quality->load();
    if (quality != activeQuality)
    {
        activeQuality = quality;
        pitchLatency = quality == 1 ? studioShifter.getLatencySamples() : 0;
        studioShifter.reset();
        liveShifter.reset();
        dryDelay.reset();
        pitchDryDelay.reset();
        const int total = computeLatency (quality);
        dryDelay.setDelay ((float) total);
        pitchDryDelay.setDelay ((float) pitchLatency);
        pendingLatency = total;
        triggerAsyncUpdate();
    }

    // ---- Input metering
    for (int ch = 0; ch < numChannels; ++ch)
        updatePeak (meters.input[(size_t) ch], buffer.getMagnitude (ch, 0, numSamples));

    // ---- Latency-aligned dry copy (for Mix and Bypass)
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        float* dst = dryBuffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            dryDelay.pushSample (ch, src[i]);
            dst[i] = dryDelay.popSample (ch);
        }
    }

    // ---- VOLTAGE: pitch + modulation
    processPitch (buffer, numChannels, numSamples);

    // ---- TONE
    tone.setParams (p.lowCut->load(), p.highCut->load(), p.mid->load());
    tone.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // ---- DRIVE (4x oversampled)
    drive.setDrive (p.drive->load() * 0.01f);
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);
        drive.process (block);
    }

    // ---- SWARM
    const bool swarmOn = p.swarmOn->load() > 0.5f;
    swarmChorus.setParams (p.swarmRate->load(), p.swarmDepth->load() * 0.01f,
                           swarmOn ? p.swarmMix->load() * 0.01f : 0.0f, p.swarmDeep->load() > 0.5f);
    swarmChorus.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // ---- MIX (equal power)
    mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
    for (int i = 0; i < numSamples; ++i)
    {
        float dryG, wetG;
        swarm::equalPowerGains (mixSmoothed.getNextValue(), dryG, wetG);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            d[i] = dryG * dryBuffer.getSample (ch, i) + wetG * d[i];
        }
    }

    // ---- FLOW (gate over the whole signal)
    const bool flowOn = p.flowOn->load() > 0.5f;
    flow.setParams (flowOn ? p.flowAmount->load() * 0.01f : 0.0f, p.flowHard->load() > 0.5f);

    bool synced = false;
    if (p.flowSync->load() > 0.5f)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                const double bpm = pos->getBpm().orFallback (120.0);
                std::optional<double> ppq;
                if (pos->getIsPlaying())
                    if (auto q = pos->getPpqPosition())
                        ppq = *q - (double) getLatencySamples() / currentSampleRate * bpm / 60.0; // align with PDC

                flow.setSynced (ParamChoices::divisionInBeats ((int) p.flowDiv->load()), bpm, ppq);
                synced = true;
            }
        }
        if (! synced)
        {
            flow.setSynced (ParamChoices::divisionInBeats ((int) p.flowDiv->load()), 120.0, std::nullopt);
            synced = true;
        }
    }
    if (! synced)
        flow.setRateHz (p.flowSpeed->load());

    if (! flow.isIdle() || flowOn)
        flow.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // ---- OUTPUT gain + bypass crossfade
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
    bypassSmoothed.setTargetValue (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGainSmoothed.getNextValue();
        const float b = bypassSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float wet = d[i] * g;
            d[i] = wet + b * (dryBuffer.getSample (ch, i) - wet);
        }
    }

    // Safety: never let a NaN/Inf escape into the host.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }

    for (int ch = 0; ch < numChannels; ++ch)
        updatePeak (meters.output[(size_t) ch], buffer.getMagnitude (ch, 0, numSamples));
}

//==============================================================================
juce::AudioProcessorEditor* SwarmnessAudioProcessor::createEditor()
{
    return new SwarmnessAudioProcessorEditor (*this);
}

void SwarmnessAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presetManager->getCurrentPresetName(), nullptr);
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    state.setProperty ("uiScale", uiScale.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SwarmnessAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            uiScale = juce::jlimit (0.7f, 2.0f, (float) tree.getProperty ("uiScale", 1.0f));
            apvts.replaceState (tree);
            presetManager->restoreFromState (tree.getProperty ("presetName").toString());
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwarmnessAudioProcessor();
}
