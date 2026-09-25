#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    inline void updatePeak (std::atomic<float>& meter, float value) noexcept
    {
        auto prev = meter.load (std::memory_order_relaxed);
        while (value > prev && ! meter.compare_exchange_weak (prev, value, std::memory_order_relaxed)) {}
    }

    inline bool on (const std::atomic<float>* v) noexcept { return v->load() > 0.5f; }
    inline float pct (const std::atomic<float>* v) noexcept { return v->load() * 0.01f; }
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

    namespace id = ParamIDs;
    p.oct1 = get (id::oct1);             p.oct2 = get (id::oct2);               p.noiseDown = get (id::noiseDown);
    p.rise = get (id::rise);             p.panic = get (id::panic);             p.chaos = get (id::chaos);
    p.speed = get (id::speed);           p.fall = get (id::fall);
    p.rbOn = get (id::rbOn);             p.rbPitch = get (id::rbPitch);         p.rbSnap = get (id::rbSnap);
    p.rbPrimary = get (id::rbPrimary);   p.rbSecondary = get (id::rbSecondary); p.rbTone = get (id::rbTone);
    p.rbTracking = get (id::rbTracking); p.rbMagic = get (id::rbMagic);         p.magicHold = get (id::magicHold);
    p.linkOct1 = get (id::linkOct1);     p.linkOct2 = get (id::linkOct2);
    p.swarmOn = get (id::swarmOn);       p.swarmDeep = get (id::swarmDeep);     p.swarmRate = get (id::swarmRate);
    p.swarmDepth = get (id::swarmDepth); p.swarmMix = get (id::swarmMix);
    p.fuzzOn = get (id::fuzzOn);         p.fuzzPost = get (id::fuzzPost);       p.fuzz = get (id::fuzz);
    p.fuzzTone = get (id::fuzzTone);     p.fuzzGate = get (id::fuzzGate);
    p.flowOn = get (id::flowOn);         p.flowHard = get (id::flowHard);       p.flowSync = get (id::flowSync);
    p.flowAmount = get (id::flowAmount); p.flowSpeed = get (id::flowSpeed);     p.flowDiv = get (id::flowDiv);
    p.mix = get (id::mix);               p.output = get (id::output);           p.bypass = get (id::bypass);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id::bypass));
    jassert (bypassParam != nullptr);

    presetManager = std::make_unique<PresetManager> (apvts);
}

//==============================================================================
bool SwarmnessAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SwarmnessAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    fuzzPre .prepare (sampleRate, maxBlockSize);
    fuzzPost.prepare (sampleRate, maxBlockSize);
    noise   .prepare (sampleRate, maxBlockSize);
    rainbow .prepare (sampleRate, maxBlockSize);
    swarmChorus.prepare (sampleRate);
    flow.prepare (sampleRate);

    const int latency = fuzzPre.getLatencySamples() + fuzzPost.getLatencySamples();
    dryDelay.setMaximumDelayInSamples (latency + 8);
    dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    dryDelay.setDelay ((float) latency);
    dryBuffer.setSize (2, maxBlockSize, false, false, true);

    auto init = [sampleRate] (juce::SmoothedValue<float>& s, double seconds, float value)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };
    init (mixSmoothed,        0.03, pct (p.mix));
    init (outputGainSmoothed, 0.03, juce::Decibels::decibelsToGain (p.output->load()));
    init (bypassSmoothed,     0.02, on (p.bypass) ? 1.0f : 0.0f);

    setLatencySamples (latency);

    for (auto& m : meters.input)  m = 0.0f;
    for (auto& m : meters.output) m = 0.0f;
}

void SwarmnessAudioProcessor::releaseResources()
{
    fuzzPre.reset();
    fuzzPost.reset();
    noise.reset();
    rainbow.reset();
    swarmChorus.reset();
    dryDelay.reset();
}

//==============================================================================
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

    auto* const* audio = buffer.getArrayOfWritePointers();

    for (int ch = 0; ch < numChannels; ++ch)
        updatePeak (meters.input[(size_t) ch], buffer.getMagnitude (ch, 0, numSamples));

    // Latency-aligned dry copy (for Mix and Bypass)
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

    // ---- FUZZ (pre)
    // Footswitches behave like real momentary pedals: holding one always engages its effect,
    // even while the plug-in is bypassed (ON off) or the RAINBOW section is switched off.
    const bool magicHeld = on (p.magicHold);
    // LINK mini switches: the VENOM (magic) footswitch can drag the octaves in with it.
    const bool oct1Held = on (p.oct1) || (magicHeld && on (p.linkOct1));
    const bool oct2Held = on (p.oct2) || (magicHeld && on (p.linkOct2));
    const bool anySwitchHeld = oct1Held || oct2Held || magicHeld;

    const bool fuzzOn = on (p.fuzzOn), fuzzIsPost = on (p.fuzzPost);
    fuzzPre.setParams (fuzzOn && ! fuzzIsPost, pct (p.fuzz), pct (p.fuzzTone), pct (p.fuzzGate));
    fuzzPre.process (audio, numChannels, numSamples);

    // ---- NOISE (footswitch octaves)
    {
        const float dir = on (p.noiseDown) ? -1.0f : 1.0f;
        const float interval = oct2Held ? 24.0f : (oct1Held ? 12.0f : 0.0f);
        noise.setParams (p.rise->load(), p.fall->load(), pct (p.panic), pct (p.chaos), pct (p.speed));
        noise.setInterval (dir * interval);
        noise.process (audio, numChannels, numSamples);
        meters.pitchSemitones.store (noise.getCurrentSemitones(), std::memory_order_relaxed);
        meters.noiseEngaged.store (noise.isEngaged(), std::memory_order_relaxed);
    }

    // ---- RAINBOW
    {
        float pitch = p.rbPitch->load();
        if (on (p.rbSnap))
            pitch = std::round (pitch);
        rainbow.setParams (on (p.rbOn) || magicHeld, pitch, pct (p.rbPrimary), pct (p.rbSecondary), pct (p.rbTone),
                           pct (p.rbTracking), pct (p.rbMagic), magicHeld);
        rainbow.process (audio, numChannels, numSamples);
    }

    // ---- SWARM
    swarmChorus.setParams (p.swarmRate->load(), pct (p.swarmDepth), on (p.swarmOn) ? pct (p.swarmMix) : 0.0f, on (p.swarmDeep));
    swarmChorus.process (audio, numChannels, numSamples);

    // ---- FUZZ (post)
    fuzzPost.setParams (fuzzOn && fuzzIsPost, pct (p.fuzz), pct (p.fuzzTone), pct (p.fuzzGate));
    fuzzPost.process (audio, numChannels, numSamples);

    // ---- MIX (linear: the chain output is often correlated with the dry signal)
    mixSmoothed.setTargetValue (pct (p.mix));
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = dryBuffer.getSample (ch, i);
            audio[ch][i] = dry + m * (audio[ch][i] - dry);
        }
    }

    // ---- FLOW (gate over the whole signal)
    const bool flowOn = on (p.flowOn);
    flow.setParams (flowOn ? pct (p.flowAmount) : 0.0f, on (p.flowHard));

    if (on (p.flowSync))
    {
        double bpm = 120.0;
        std::optional<double> ppq;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                bpm = pos->getBpm().orFallback (120.0);
                if (pos->getIsPlaying())
                    if (auto q = pos->getPpqPosition())
                        ppq = *q - (double) getLatencySamples() / currentSampleRate * bpm / 60.0; // align with PDC
            }
        flow.setSynced (ParamChoices::divisionInBeats ((int) p.flowDiv->load()), bpm, ppq);
    }
    else
    {
        flow.setRateHz (p.flowSpeed->load());
    }

    if (flowOn || ! flow.isIdle())
        flow.process (audio, numChannels, numSamples);

    // ---- OUTPUT gain + bypass crossfade
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
    // (the NOISE return glide after releasing a footswitch is allowed to finish, too)
    bypassSmoothed.setTargetValue (on (p.bypass) && ! anySwitchHeld && ! noise.isEngaged() ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGainSmoothed.getNextValue();
        const float b = bypassSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float wet = audio[ch][i] * g;
            audio[ch][i] = wet + b * (dryBuffer.getSample (ch, i) - wet);
        }
    }

    // Safety: never let a NaN/Inf escape, and keep self-oscillation from blowing up speakers.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* d = audio[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
            else if (std::abs (d[i]) > 2.0f)
                d[i] = 2.0f * std::tanh (d[i] * 0.5f);
        }
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

            // Momentary footswitches must never come back "stuck down" after reloading a session.
            if (apvts.getRawParameterValue (ParamIDs::switchMode)->load() < 0.5f)
                for (auto* id : { ParamIDs::oct1, ParamIDs::oct2, ParamIDs::magicHold })
                    if (auto* param = apvts.getParameter (id))
                        param->setValueNotifyingHost (0.0f);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwarmnessAudioProcessor();
}
