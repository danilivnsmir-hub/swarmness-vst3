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
    p.speed = get (id::speed);           p.fall = get (id::fall);               p.stingMix = get (id::stingMix);
    p.stingRaw = get (id::stingRaw);     p.stingDetune = get (id::stingDetune); p.rbDetune = get (id::rbDetune);     p.rbRaw = get (id::rbRaw);
    p.rbOn = get (id::rbOn);             p.rbPitch = get (id::rbPitch);         p.rbSnap = get (id::rbSnap);
    p.rbPrimary = get (id::rbPrimary);   p.rbSecondary = get (id::rbSecondary); p.rbTone = get (id::rbTone);
    p.rbTracking = get (id::rbTracking); p.rbMagic = get (id::rbMagic);         p.magicHold = get (id::magicHold);
    p.linkOct1 = get (id::linkOct1);     p.linkOct2 = get (id::linkOct2);
    p.rbMix = get (id::rbMix);           p.rbTime = get (id::rbTime);         p.rbSync = get (id::rbSync);           p.rbDiv = get (id::rbDiv);
    p.swarmOn = get (id::swarmOn);       p.swarmDeep = get (id::swarmDeep);     p.swarmRate = get (id::swarmRate);
    p.swarmDepth = get (id::swarmDepth); p.swarmMix = get (id::swarmMix);
    p.fuzzOn = get (id::fuzzOn);         p.fuzz = get (id::fuzz);
    p.fuzzTone = get (id::fuzzTone);     p.fuzzGate = get (id::fuzzGate);       p.fuzzVoice = get (id::fuzzVoice);
    p.fuzzScoop = get (id::fuzzScoop);   p.fuzzGlare = get (id::fuzzGlare);     p.fuzzBlend = get (id::fuzzBlend);   p.fuzzSag = get (id::fuzzSag);
    p.flowOn = get (id::flowOn);         p.flowHard = get (id::flowHard);       p.flowSync = get (id::flowSync);
    p.flowAmount = get (id::flowAmount); p.flowSpeed = get (id::flowSpeed);     p.flowDiv = get (id::flowDiv);
    p.output = get (id::output);         p.input = get (id::input);           p.bypass = get (id::bypass);

    p.geqOn = get (id::geqOn);           p.geqLevel = get (id::geqLevel);
    for (int b = 0; b < swarm::GraphicEq::numBands; ++b)
        p.geqBands[(size_t) b] = get (id::geqBands[b]);
    p.peqOn = get (id::peqOn);           p.peqHpFreq = get (id::peqHpFreq);     p.peqLpFreq = get (id::peqLpFreq);
    p.peqLowFreq = get (id::peqLowFreq); p.peqLowGain = get (id::peqLowGain);   p.peqHighFreq = get (id::peqHighFreq); p.peqHighGain = get (id::peqHighGain);
    p.peqBellFreq = { get (id::peqB1Freq), get (id::peqB2Freq), get (id::peqB3Freq) };
    p.peqBellGain = { get (id::peqB1Gain), get (id::peqB2Gain), get (id::peqB3Gain) };
    p.peqBellQ    = { get (id::peqB1Q),    get (id::peqB2Q),    get (id::peqB3Q) };
    p.revOn = get (id::revOn);           p.revType = get (id::revType);         p.revMix = get (id::revMix);
    p.revDecay = get (id::revDecay);     p.revSize = get (id::revSize);         p.revPreDelay = get (id::revPreDelay);
    p.revTone = get (id::revTone);       p.revLowCut = get (id::revLowCut);     p.revMod = get (id::revMod);         p.revDuck = get (id::revDuck);
    for (int b = 0; b < Chain::numBlocks; ++b)
        p.chainSlots[(size_t) b] = get (Chain::slotIds[b]);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id::bypass));
    jassert (bypassParam != nullptr);

    presetManager = std::make_unique<PresetManager> (apvts);
    presetManager->onSaveExtras = [this] (juce::DynamicObject& json)
    {
        const auto ir = getReverbIRFile();
        if (ir != juce::File())
            json.setProperty ("reverbIR", ir.getFullPathName());
    };
    presetManager->onLoadExtras = [this] (const juce::var& json)
    {
        // A preset that used an impulse response brings it back (if the file is still there).
        const auto path = json["reverbIR"].toString();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
            loadReverbIR (juce::File (path));
    };
}

//==============================================================================
bool SwarmnessAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // mono -> mono, stereo -> stereo, and mono guitar -> stereo (SWARM / HIVE spread)
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void SwarmnessAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    fuzzStage.prepare (sampleRate, maxBlockSize);
    noise    .prepare (sampleRate, maxBlockSize);
    rainbow  .prepare (sampleRate, maxBlockSize);
    swarmChorus.prepare (sampleRate);
    flow .prepare (sampleRate);
    comb .prepare (sampleRate);
    carve.prepare (sampleRate);
    crypt.prepare (sampleRate, maxBlockSize);

    // Only SMOKE (oversampling) adds latency, and it is there wherever SMOKE sits in the chain.
    const int latency = fuzzStage.getLatencySamples();
    dryDelay.setMaximumDelayInSamples (latency + 8);
    dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    dryDelay.setDelay ((float) latency);
    dryBuffer.setSize (2, maxBlockSize, false, false, true);
    inputGainTrack.assign ((size_t) maxBlockSize, 1.0f);
    hiveBuffer.setSize (2, maxBlockSize, false, false, true);

    auto init = [sampleRate] (juce::SmoothedValue<float>& s, double seconds, float value)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };
    init (outputGainSmoothed, 0.03, juce::Decibels::decibelsToGain (p.output->load()));
    init (inputGainSmoothed,  0.03, juce::Decibels::decibelsToGain (p.input->load()));
    init (hiveVoiceGain,      0.02, 1.0f);
    init (bypassSmoothed,     0.02, on (p.bypass) ? 1.0f : 0.0f);
    init (chainFade,          0.008, 1.0f);
    activeOrder = getRequestedChainOrder();

    setLatencySamples (latency);

    for (auto& m : meters.input)  m = 0.0f;
    for (auto& m : meters.output) m = 0.0f;
}

void SwarmnessAudioProcessor::releaseResources()
{
    fuzzStage.reset();
    noise.reset();
    rainbow.reset();
    swarmChorus.reset();
    comb.reset();
    carve.reset();
    crypt.reset();
    dryDelay.reset();
}

//==============================================================================
void SwarmnessAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    handleMidi (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    // Mono in, stereo out: duplicate the guitar into both channels; other extra outputs are cleared.
    const int numIns = getTotalNumInputChannels();
    for (int i = numIns; i < getTotalNumOutputChannels(); ++i)
    {
        if (numIns == 1 && i == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        else
            buffer.clear (i, 0, numSamples);
    }

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

    // Host tempo / position (for the tempo-synced HIVE repeats and WINGS gate)
    struct { double bpm = 120.0; std::optional<double> ppq; } transport;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            transport.bpm = juce::jlimit (20.0, 400.0, pos->getBpm().orFallback (120.0));
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition())
                    transport.ppq = *q;
        }

    // ---- INPUT sensitivity (undone at the output, so it only changes how hard the effects are hit)
    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.input->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGainSmoothed.getNextValue();
        inputGainTrack[(size_t) i] = g;
        for (int ch = 0; ch < numChannels; ++ch)
            audio[ch][i] *= g;
    }

    for (int ch = 0; ch < numChannels; ++ch)
        updatePeak (meters.input[(size_t) ch], buffer.getMagnitude (ch, 0, numSamples));

    // Footswitches behave like real momentary pedals: holding one always engages its effect,
    // even while the plug-in is bypassed (ON off) or the HIVE section is switched off.
    BlockContext ctx;
    ctx.bpm = transport.bpm;
    ctx.ppq = transport.ppq;
    ctx.magicHeld = on (p.magicHold);
    // LINK mini switches: the VENOM (magic) footswitch can drag the octaves in with it.
    ctx.oct1Held = on (p.oct1) || (ctx.magicHeld && on (p.linkOct1));
    ctx.oct2Held = on (p.oct2) || (ctx.magicHeld && on (p.linkOct2));
    const bool anySwitchHeld = ctx.oct1Held || ctx.oct2Held || ctx.magicHeld;

    // ---- the chain. A new order is faded in: dip the output, swap, come back (~8 ms each way).
    if (getRequestedChainOrder() != activeOrder && chainFade.getTargetValue() > 0.5f)
        chainFade.setTargetValue (0.0f);

    for (int block : activeOrder)
        processChainBlock (block, ctx, audio, numChannels, numSamples);

    if (chainFade.isSmoothing() || chainFade.getCurrentValue() < 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = chainFade.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] *= g;
        }
        if (chainFade.getCurrentValue() <= 0.0f && ! chainFade.isSmoothing())
        {
            activeOrder = getRequestedChainOrder();
            chainFade.setTargetValue (1.0f);
        }
    }
    meters.reverbLevel.store (juce::jmax (meters.reverbLevel.load (std::memory_order_relaxed), crypt.getWetLevel()), std::memory_order_relaxed);

    // ---- OUTPUT gain + bypass crossfade
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
    // (the NOISE return glide after releasing a footswitch is allowed to finish, too)
    bypassSmoothed.setTargetValue (on (p.bypass) && ! anySwitchHeld && ! noise.isEngaged() ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        // Output gain, with the INPUT sensitivity undone
        const float g = outputGainSmoothed.getNextValue() / inputGainTrack[(size_t) i];
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

    spectrumTap.push (audio[0], numChannels > 1 ? audio[1] : nullptr, numSamples);
}

//==============================================================================
void SwarmnessAudioProcessor::processChainBlock (int block, const BlockContext& ctx, float* const* audio, int numChannels, int numSamples) noexcept
{
    switch (block)
    {
        case Chain::pitch: processPitch (ctx, audio, numChannels, numSamples); break;
        case Chain::smoke: processSmoke (audio, numChannels, numSamples); break;
        case Chain::wings: processWings (ctx, audio, numChannels, numSamples); break;

        case Chain::swarm:
            swarmChorus.setParams (p.swarmRate->load(), pct (p.swarmDepth), on (p.swarmOn) ? pct (p.swarmMix) : 0.0f, on (p.swarmDeep));
            swarmChorus.process (audio, numChannels, numSamples);
            break;

        case Chain::comb:
        {
            std::array<float, swarm::GraphicEq::numBands> gains {};
            for (size_t b = 0; b < gains.size(); ++b)
                gains[b] = p.geqBands[b]->load();
            comb.setParams (on (p.geqOn), gains, p.geqLevel->load());
            comb.process (audio, numChannels, numSamples);
            break;
        }

        case Chain::carve:
        {
            swarm::ParametricEq::Settings s;
            s.hpHz = p.peqHpFreq->load();
            s.lpHz = p.peqLpFreq->load();
            s.lowHz = p.peqLowFreq->load();
            s.lowDb = p.peqLowGain->load();
            s.highHz = p.peqHighFreq->load();
            s.highDb = p.peqHighGain->load();
            for (size_t b = 0; b < 3; ++b)
            {
                s.bellHz[b] = p.peqBellFreq[b]->load();
                s.bellDb[b] = p.peqBellGain[b]->load();
                s.bellQ[b]  = p.peqBellQ[b]->load();
            }
            carve.setParams (on (p.peqOn), s);
            carve.process (audio, numChannels, numSamples);
            break;
        }

        case Chain::crypt:
        {
            ReverbStage::Settings s;
            s.on = on (p.revOn);
            s.type = (int) p.revType->load();
            s.mix = pct (p.revMix);
            s.decay = p.revDecay->load();
            s.size = pct (p.revSize);
            s.preDelayMs = p.revPreDelay->load();
            s.tone = pct (p.revTone);
            s.lowCutHz = p.revLowCut->load();
            s.mod = pct (p.revMod);
            s.duck = pct (p.revDuck);
            crypt.setParams (s);
            crypt.process (audio, numChannels, numSamples);
            break;
        }

        default: break;
    }
}

void SwarmnessAudioProcessor::processSmoke (float* const* audio, int numChannels, int numSamples) noexcept
{
    FuzzStage::Settings s;
    s.fuzz  = pct (p.fuzz);
    s.tone  = pct (p.fuzzTone);
    s.scoop = pct (p.fuzzScoop);
    s.glare = pct (p.fuzzGlare);
    s.gate  = pct (p.fuzzGate);
    s.blend = pct (p.fuzzBlend);
    s.sag   = pct (p.fuzzSag);
    s.voice = (int) p.fuzzVoice->load();
    fuzzStage.setParams (on (p.fuzzOn), s);
    fuzzStage.process (audio, numChannels, numSamples);
}

void SwarmnessAudioProcessor::processPitch (const BlockContext& ctx, float* const* audio, int numChannels, int numSamples) noexcept
{
    // STING and HIVE run in parallel from the same (played) signal, so HIVE harmonises the note
    // you play - not the STING octave - and the TRAILS never pick up the octave.
    for (int ch = 0; ch < numChannels; ++ch)
        hiveBuffer.copyFrom (ch, 0, audio[ch], numSamples);

    // HIVE MIX (pedal law): 50% = dry and voices both full, 100% = voices only. It only turns down
    // the dry part, so the STING octave still sounds on top when a footswitch is held.
    const bool hiveOn = on (p.rbOn) || ctx.magicHeld;
    const float hiveMix = pct (p.rbMix);
    noise.setDryLevel (hiveOn ? juce::jmin (1.0f, 2.0f * (1.0f - hiveMix)) : 1.0f);
    hiveVoiceGain.setTargetValue (hiveOn ? juce::jmin (1.0f, 2.0f * hiveMix) : 1.0f);

    // ---- STING (footswitch octaves)
    {
        const float dir = on (p.noiseDown) ? -1.0f : 1.0f;
        const float interval = ctx.oct2Held ? 24.0f : (ctx.oct1Held ? 12.0f : 0.0f);
        noise.setParams (p.rise->load(), p.fall->load(), pct (p.panic), pct (p.chaos), pct (p.speed), pct (p.stingMix), on (p.stingRaw));
        noise.setInterval (dir * interval);
        noise.setDetuneCents (p.stingDetune->load());
        noise.process (audio, numChannels, numSamples);
        meters.pitchSemitones.store (noise.getCurrentSemitones(), std::memory_order_relaxed);
        meters.noiseEngaged.store (noise.isEngaged(), std::memory_order_relaxed);
    }

    // ---- HIVE
    {
        float pitch = p.rbPitch->load();
        if (on (p.rbSnap))
            pitch = std::round (pitch);
        double repeatSeconds = p.rbTime->load() * 0.001;
        if (on (p.rbSync))
            repeatSeconds = ParamChoices::divisionInBeats ((int) p.rbDiv->load()) * 60.0 / ctx.bpm;
        rainbow.setParams (hiveOn, pitch, pct (p.rbPrimary), pct (p.rbSecondary), pct (p.rbTone),
                           pct (p.rbTracking), pct (p.rbMagic), (float) repeatSeconds, ctx.magicHeld, on (p.rbRaw),
                           p.rbDetune->load());
        float* hive[2] = { hiveBuffer.getWritePointer (0), hiveBuffer.getWritePointer (1) };
        rainbow.process (hive, numChannels, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = hiveVoiceGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] += g * hive[ch][i];
        }
    }
}

void SwarmnessAudioProcessor::processWings (const BlockContext& ctx, float* const* audio, int numChannels, int numSamples) noexcept
{
    const bool flowOn = on (p.flowOn);
    flow.setParams (flowOn ? pct (p.flowAmount) : 0.0f, on (p.flowHard));

    if (on (p.flowSync))
    {
        std::optional<double> ppq;
        if (ctx.ppq.has_value())
            ppq = *ctx.ppq - (double) getLatencySamples() / currentSampleRate * ctx.bpm / 60.0; // align with PDC
        flow.setSynced (ParamChoices::divisionInBeats ((int) p.flowDiv->load()), ctx.bpm, ppq);
    }
    else
    {
        flow.setRateHz (p.flowSpeed->load());
    }

    if (flowOn || ! flow.isIdle())
        flow.process (audio, numChannels, numSamples);
}

//==============================================================================
Chain::Order SwarmnessAudioProcessor::getRequestedChainOrder() const noexcept
{
    std::array<float, Chain::numBlocks> slots {};
    for (size_t b = 0; b < slots.size(); ++b)
        slots[b] = p.chainSlots[b]->load();
    return Chain::orderFromSlots (slots);
}

void SwarmnessAudioProcessor::setChainOrder (const Chain::Order& order)
{
    const auto slots = Chain::slotsForOrder (order);
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (auto* param = apvts.getParameter (Chain::slotIds[b]))
        {
            const float norm = param->convertTo0to1 (slots[(size_t) b]);
            if (std::abs (param->getValue() - norm) > 1.0e-6f)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (norm);
                param->endChangeGesture();
            }
        }
}

//==============================================================================
juce::String SwarmnessAudioProcessor::loadReverbIR (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Can't read \"" + file.getFileName() + "\" (WAV / AIFF / FLAC / OGG)";

    // Up to 12 s (long enough for any hall or cathedral); stereo IRs keep their width.
    const auto maxLength = (juce::int64) (reader->sampleRate * 12.0);
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);
    if (length < 16 || reader->sampleRate <= 0.0)
        return "\"" + file.getFileName() + "\" is too short";

    const int channels = reader->numChannels > 1 ? 2 : 1;
    juce::AudioBuffer<float> ir (2, length);
    reader->read (&ir, 0, length, 0, true, channels > 1);
    if (channels == 1)
        ir.copyFrom (1, 0, ir, 0, 0, length);

    // Envelope for the editor (peak per slice, normalised)
    std::vector<float> envelope (256, 0.0f);
    const int slice = juce::jmax (1, length / (int) envelope.size());
    float maxPeak = 1.0e-9f;
    for (size_t k = 0; k < envelope.size(); ++k)
    {
        const int from = (int) k * slice;
        if (from >= length) break;
        const int n = juce::jmin (slice, length - from);
        envelope[k] = juce::jmax (ir.getMagnitude (0, from, n), ir.getMagnitude (1, from, n));
        maxPeak = juce::jmax (maxPeak, envelope[k]);
    }
    for (auto& v : envelope)
        v /= maxPeak;

    const double seconds = (double) length / reader->sampleRate;
    crypt.setImpulseResponse (std::move (ir), reader->sampleRate);

    const juce::ScopedLock sl (irInfoLock);
    reverbIREnvelope = std::move (envelope);
    reverbIRSeconds = seconds;
    reverbIRFile = file;
    reverbIRDescription = file.getFileNameWithoutExtension() + "  -  " + juce::String ((double) length / reader->sampleRate, 1) + " s, "
                        + (channels > 1 ? "stereo" : "mono");
    return {};
}

void SwarmnessAudioProcessor::clearReverbIR()
{
    crypt.clearImpulseResponse();
    const juce::ScopedLock sl (irInfoLock);
    reverbIRFile = juce::File();
    reverbIRDescription.clear();
    reverbIREnvelope.clear();
    reverbIRSeconds = 0.0;
}

std::vector<float> SwarmnessAudioProcessor::getReverbIREnvelope() const
{
    const juce::ScopedLock sl (irInfoLock);
    return reverbIREnvelope;
}

double SwarmnessAudioProcessor::getReverbIRSeconds() const
{
    const juce::ScopedLock sl (irInfoLock);
    return reverbIRSeconds;
}

juce::File SwarmnessAudioProcessor::getReverbIRFile() const
{
    const juce::ScopedLock sl (irInfoLock);
    return reverbIRFile;
}

juce::String SwarmnessAudioProcessor::getReverbIRDescription() const
{
    const juce::ScopedLock sl (irInfoLock);
    return reverbIRDescription;
}

//==============================================================================
static const char* midiTargetParam (int target)
{
    switch (target)
    {
        case 0:  return ParamIDs::oct1;
        case 1:  return ParamIDs::oct2;
        case 2:  return ParamIDs::magicHold;
        default: return ParamIDs::bypass;
    }
}

void SwarmnessAudioProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const bool isCC = msg.isController(), isNote = msg.isNoteOnOrOff();
        if (! isCC && ! isNote)
            continue;

        const int number = isCC ? msg.getControllerNumber() : msg.getNoteNumber();
        const int kind   = isCC ? (int) MidiKind::cc : (int) MidiKind::note;
        const bool pressed = isCC ? msg.getControllerValue() >= 64 : msg.isNoteOn();

        // Learn: the first CC / note-on after "MIDI Learn" becomes the binding
        const int learn = midiLearnTarget.load();
        if (learn >= 0 && (isCC || msg.isNoteOn()))
        {
            midiMap[(size_t) learn].kind.store (kind);
            midiMap[(size_t) learn].number.store (number);
            midiMap[(size_t) learn].down.store (false);
            midiLearnTarget.store (-1);
            continue;
        }

        for (int t = 0; t < kNumMidiTargets; ++t)
        {
            auto& b = midiMap[(size_t) t];
            if (b.kind.load() == kind && b.number.load() == number && b.down.load() != pressed)
            {
                b.down.store (pressed);
                applyFootswitch (t, pressed);
            }
        }
    }
}

void SwarmnessAudioProcessor::applyFootswitch (int target, bool pressed)
{
    auto* param = apvts.getParameter (midiTargetParam (target));
    if (param == nullptr)
        return;

    const bool momentary = apvts.getRawParameterValue (ParamIDs::switchMode)->load() < 0.5f;
    if (target < 3 && momentary)
        param->setValueNotifyingHost (pressed ? 1.0f : 0.0f);      // held = on
    else if (pressed)
        param->setValueNotifyingHost (param->getValue() >= 0.5f ? 0.0f : 1.0f);   // latch / ON: toggle per press
}

juce::String SwarmnessAudioProcessor::describeMidiBinding (int target) const
{
    const auto& b = midiMap[(size_t) juce::jlimit (0, kNumMidiTargets - 1, target)];
    const int n = b.number.load();
    switch ((MidiKind) b.kind.load())
    {
        case MidiKind::cc:   return "CC " + juce::String (n);
        case MidiKind::note: return "Note " + juce::MidiMessage::getMidiNoteName (n, true, true, 3);
        case MidiKind::none: break;
    }
    return {};
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
    state.setProperty ("reverbIR", getReverbIRFile().getFullPathName(), nullptr);
    for (int t = 0; t < kNumMidiTargets; ++t)
        state.setProperty ("midi" + juce::String (t),
                           juce::String (midiMap[(size_t) t].kind.load()) + ":" + juce::String (midiMap[(size_t) t].number.load()), nullptr);

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
            for (int t = 0; t < kNumMidiTargets; ++t)
            {
                const auto v = tree.getProperty ("midi" + juce::String (t), "0:-1").toString();
                midiMap[(size_t) t].kind.store (juce::jlimit (0, 2, v.upToFirstOccurrenceOf (":", false, false).getIntValue()));
                midiMap[(size_t) t].number.store (v.fromFirstOccurrenceOf (":", false, false).getIntValue());
                midiMap[(size_t) t].down.store (false);
            }
            // Sessions from before the chain: SMOKE "POST" becomes SMOKE placed after SWARM.
            const auto legacyPost = tree.getChildWithProperty ("id", ParamIDs::fuzzPostLegacy);
            const bool migrateSmoke = legacyPost.isValid() && (float) legacyPost.getProperty ("value", 0.0f) > 0.5f
                                      && ! tree.getChildWithProperty ("id", Chain::slotIds[Chain::smoke]).isValid();

            apvts.replaceState (tree);

            if (migrateSmoke)
                if (auto* slot = apvts.getParameter (Chain::slotIds[Chain::smoke]))
                    slot->setValueNotifyingHost (slot->convertTo0to1 ((float) Chain::legacyPostSmokeSlot));

            const auto irPath = tree.getProperty ("reverbIR").toString();
            if (irPath.isNotEmpty() && juce::File::isAbsolutePath (irPath) && juce::File (irPath).existsAsFile())
                loadReverbIR (juce::File (irPath));
            else
                clearReverbIR();

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
