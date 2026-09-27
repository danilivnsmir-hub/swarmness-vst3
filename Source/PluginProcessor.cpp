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
    p.oct1 = get (id::oct1);             p.oct2 = get (id::oct2);               p.shiftA = get (id::shiftA);  p.shiftB = get (id::shiftB);
    p.shStack = get (id::shStack);       p.shSnap = get (id::shSnap);           p.shRaw = get (id::shRaw);    p.shDetune = get (id::shDetune);
    p.hvMangle = get (id::hvMangle);
    p.trSteps = get (id::trSteps);       p.trChop = get (id::trChop);         p.trDry = get (id::trDry);             p.shOn = get (id::shOn);
    for (int k = 0; k < 16; ++k)
    {
        p.trLevels[(size_t) k] = get (id::trLevels[k]);
        p.trMoves[(size_t) k] = get (id::trMoves[k]);
    }
    p.rise = get (id::rise);             p.panic = get (id::panic);             p.chaos = get (id::chaos);
    p.speed = get (id::speed);           p.fall = get (id::fall);               p.stingMix = get (id::stingMix);
    p.rbDetune = get (id::rbDetune);     p.rbRaw = get (id::rbRaw);
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
    p.ampOn = get (id::ampOn);           p.ampChannel = get (id::ampChannel);   p.ampChar = get (id::ampChar);       p.ampGain = get (id::ampGain);
    p.ampBass = get (id::ampBass);       p.ampMid = get (id::ampMid);           p.ampTreble = get (id::ampTreble);   p.ampPresence = get (id::ampPresence);
    p.ampDepth = get (id::ampDepth);     p.ampMaster = get (id::ampMaster);     p.ampGate = get (id::ampGate);       p.ampLevel = get (id::ampLevel);
    p.cabOn = get (id::cabOn);           p.cabType = get (id::cabType);         p.cabMic = get (id::cabMic);         p.cabDist = get (id::cabDist);
    p.cabLowCut = get (id::cabLowCut);   p.cabHighCut = get (id::cabHighCut);   p.cabLevel = get (id::cabLevel);
    for (int b = 0; b < Chain::numBlocks; ++b)
    {
        p.chainSlots[(size_t) b] = get (Chain::slotIds[b]);
        p.chainLanes[(size_t) b] = get (Chain::laneIds[b]);
    }
    for (int sp = 0; sp < Chain::maxSplits; ++sp)
        p.parMix[(size_t) sp] = get (Chain::parallelMixIds[sp]);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id::bypass));
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            learnableParams.add (ranged);
    apvts.addParameterListener (id::scene, this);
    jassert (bypassParam != nullptr);

    presetManager = std::make_unique<PresetManager> (apvts);
    presetManager->onSaveExtras = [this] (juce::DynamicObject& json)
    {
        const auto ir = getReverbIRFile();
        if (ir != juce::File())
            json.setProperty ("reverbIR", ir.getFullPathName());
        if (const auto nam = getNamModelFile(); nam != juce::File())
            json.setProperty ("namModel", nam.getFullPathName());
        if (const auto cabIr = getCabIRFile(); cabIr != juce::File())
            json.setProperty ("cabIR", cabIr.getFullPathName());
    };
    presetManager->onLoadExtras = [this] (const juce::var& json)
    {
        // A preset that used an impulse response brings it back (if the file is still there).
        const auto path = json["reverbIR"].toString();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
            loadReverbIR (juce::File (path));
        // the AMP capture and the CAB IR too (a preset without them keeps what is loaded)
        for (auto [key, isNam] : { std::pair<const char*, bool> { "namModel", true }, { "cabIR", false } })
        {
            const auto file = json[key].toString();
            if (file.isNotEmpty() && juce::File::isAbsolutePath (file) && juce::File (file).existsAsFile())
            {
                if (isNam) loadNamModel (juce::File (file));
                else       loadCabIR (juce::File (file));
            }
        }
    };
    housekeeper = std::make_unique<Housekeeper> (*this);
    housekeeper->startThread (juce::Thread::Priority::low);
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
    shift.prepare (sampleRate, maxBlockSize);
    hive.prepare (sampleRate, maxBlockSize);
    swarmChorus.prepare (sampleRate);
    flow .prepare (sampleRate);
    comb .prepare (sampleRate);
    carve.prepare (sampleRate);
    crypt.prepare (sampleRate, maxBlockSize);
    amp.prepare (sampleRate, maxBlockSize);
    cab.prepare (sampleRate, maxBlockSize);
    updateCabModel (true);

    // Only SMOKE (oversampling) adds latency, and it is there wherever SMOKE sits in the chain.
    const int latency = fuzzStage.getLatencySamples();
    dryDelay.setMaximumDelayInSamples (latency + 8);
    dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    dryDelay.setDelay ((float) latency);
    dryBuffer.setSize (2, maxBlockSize, false, false, true);
    inputGainTrack.assign ((size_t) maxBlockSize, 1.0f);

    auto init = [sampleRate] (juce::SmoothedValue<float>& s, double seconds, float value)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };
    init (outputGainSmoothed, 0.03, juce::Decibels::decibelsToGain (p.output->load()));
    init (inputGainSmoothed,  0.03, juce::Decibels::decibelsToGain (p.input->load()));
    init (bypassSmoothed,     0.02, on (p.bypass) ? 1.0f : 0.0f);
    init (chainFade,          0.008, 1.0f);
    for (int sp = 0; sp < Chain::maxSplits; ++sp)
        init (parMixSmoothed[(size_t) sp], 0.03, pct (p.parMix[(size_t) sp]));
    activeLayout = getRequestedLayout();
    pathBBuffer.setSize (2, maxBlockSize, false, false, true);
    for (auto* d : { &pathAlignA, &pathAlignB })
    {
        d->setMaximumDelayInSamples (latency + 8);
        d->prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        d->setDelay ((float) latency);
        d->reset();
    }

    setLatencySamples (latency);

    for (auto& m : meters.input)  m = 0.0f;
    for (auto& m : meters.output) m = 0.0f;
}

void SwarmnessAudioProcessor::releaseResources()
{
    fuzzStage.reset();
    shift.reset();
    hive.reset();
    swarmChorus.reset();
    comb.reset();
    carve.reset();
    crypt.reset();
    amp.reset();
    cab.reset();
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

    // ---- the chain:  pre -> split -> [path A || path B] -> merge -> post.
    // A new order / routing is faded in: dip the output, swap, come back (~8 ms each way).
    if (getRequestedLayout() != activeLayout && chainFade.getTargetValue() > 0.5f)
        chainFade.setTargetValue (0.0f);

    const auto plan = Chain::planFor (activeLayout);
    for (int sp = 0; sp < Chain::maxSplits; ++sp)
        parMixSmoothed[(size_t) sp].setTargetValue (pct (p.parMix[(size_t) sp]));

    for (int si = 0; si < plan.numStages; ++si)
    {
        const auto& stage = plan.stages[(size_t) si];
        if (! stage.parallel)
        {
            processChainBlock (stage.block, ctx, audio, numChannels, numSamples);
            continue;
        }

        // split: path A works in place, path B on a copy
        for (int ch = 0; ch < numChannels; ++ch)
            pathBBuffer.copyFrom (ch, 0, audio[ch], numSamples);
        float* pathB[2] = { pathBBuffer.getWritePointer (0), pathBBuffer.getWritePointer (1) };

        bool smokeA = false, smokeB = false;
        for (int i = 0; i < stage.numA; ++i)
        {
            smokeA = smokeA || stage.a[(size_t) i] == Chain::smoke;
            processChainBlock (stage.a[(size_t) i], ctx, audio, numChannels, numSamples);
        }
        for (int i = 0; i < stage.numB; ++i)
        {
            smokeB = smokeB || stage.b[(size_t) i] == Chain::smoke;
            processChainBlock (stage.b[(size_t) i], ctx, pathB, numChannels, numSamples);
        }

        // Keep both paths time-aligned (SMOKE oversampling) so they never comb-filter.
        // SMOKE exists once, so at most one split ever needs this.
        auto align = [numChannels, numSamples] (juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>& d, float* const* x)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                {
                    d.pushSample (ch, x[ch][i]);
                    x[ch][i] = d.popSample (ch);
                }
        };
        if (smokeA && ! smokeB) align (pathAlignB, pathB);
        if (smokeB && ! smokeA) align (pathAlignA, audio);

        // merge: linear balance, so identical paths add up to exactly the input level
        auto& mix = parMixSmoothed[(size_t) stage.split];
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mix.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] += m * (pathB[ch][i] - audio[ch][i]);
        }
    }

    // mixes of splits that are not in the chain just follow their parameter
    for (int sp = plan.numSplits; sp < Chain::maxSplits; ++sp)
        parMixSmoothed[(size_t) sp].setCurrentAndTargetValue (pct (p.parMix[(size_t) sp]));

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
            activeLayout = getRequestedLayout();
            pathAlignA.reset();
            pathAlignB.reset();
            chainFade.setTargetValue (1.0f);
        }
    }
    meters.reverbLevel.store (juce::jmax (meters.reverbLevel.load (std::memory_order_relaxed), crypt.getWetLevel()), std::memory_order_relaxed);

    // ---- OUTPUT gain + bypass crossfade
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
    // (the NOISE return glide after releasing a footswitch is allowed to finish, too)
    bypassSmoothed.setTargetValue (on (p.bypass) && ! anySwitchHeld && ! (shift.isEngaged() && ! on (p.shOn)) ? 1.0f : 0.0f);

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
        case Chain::shift: processShift (ctx, audio, numChannels, numSamples); break;
        case Chain::pitch: processHive (ctx, audio, numChannels, numSamples); break;
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

        case Chain::amp:
        {
            AmpBlock::Settings s;
            s.on = on (p.ampOn);
            s.channel = juce::jlimit (0, 3, (int) p.ampChannel->load());
            s.character = pct (p.ampChar);
            s.gain = p.ampGain->load() * 0.1f;
            s.bass = p.ampBass->load() * 0.1f;
            s.mid = p.ampMid->load() * 0.1f;
            s.treble = p.ampTreble->load() * 0.1f;
            s.presence = p.ampPresence->load() * 0.1f;
            s.depth = p.ampDepth->load() * 0.1f;
            s.master = p.ampMaster->load() * 0.1f;
            s.gate = pct (p.ampGate);
            s.levelDb = p.ampLevel->load();
            amp.setParams (s);
            amp.process (audio, numChannels, numSamples);
            break;
        }

        case Chain::cab:
        {
            CabBlock::Settings s;
            s.on = on (p.cabOn);
            s.type = juce::jlimit (0, (int) CabBlock::numTypes - 1, (int) p.cabType->load());
            s.mic = pct (p.cabMic);
            s.distance = pct (p.cabDist);
            s.lowCutHz = p.cabLowCut->load();
            s.highCutHz = p.cabHighCut->load();
            s.levelDb = p.cabLevel->load();
            cab.setParams (s);
            cab.process (audio, numChannels, numSamples);
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

void SwarmnessAudioProcessor::processShift (const BlockContext& ctx, float* const* audio, int numChannels, int numSamples) noexcept
{
    ShiftBlock::Settings s;
    s.shiftA = ctx.oct1Held || on (p.shOn);   // power button = SHIFT A latched
    s.shiftB = ctx.oct2Held;
    s.stack = on (p.shStack);
    s.shiftASemis = std::round (p.shiftA->load());
    s.shiftBSemis = std::round (p.shiftB->load());
    s.riseMs = p.rise->load();
    s.fallMs = p.fall->load();
    s.mix = pct (p.stingMix);
    s.anger = pct (p.panic);
    s.frenzy = pct (p.chaos);
    s.buzz = pct (p.speed);
    s.snap = on (p.shSnap);
    s.raw = on (p.shRaw);
    s.detuneCents = p.shDetune->load();

    shift.setParams (s);
    shift.process (audio, numChannels, numSamples);
    meters.pitchSemitones.store (shift.getSemitones(), std::memory_order_relaxed);
    meters.noiseEngaged.store (shift.isEngaged(), std::memory_order_relaxed);
    meters.stackSemitones.store (shift.getStackSemitones(), std::memory_order_relaxed);
    meters.stackOn.store (shift.isStacked(), std::memory_order_relaxed);
}

void SwarmnessAudioProcessor::processHive (const BlockContext& ctx, float* const* audio, int numChannels, int numSamples) noexcept
{
    HiveBlock::Settings s;
    s.venom = ctx.magicHeld;
    s.voicesOn = on (p.rbOn);
    s.snap = on (p.rbSnap);
    s.pitchSemis = p.rbPitch->load();
    s.drone = pct (p.rbPrimary);
    s.queen = pct (p.rbSecondary);
    s.tracking = pct (p.rbTracking);

    s.trails = pct (p.rbMagic);
    s.tone = pct (p.rbTone);
    const double divBeats = ParamChoices::divisionInBeats ((int) p.rbDiv->load());
    s.repeatSeconds = on (p.rbSync) ? (float) (divBeats * 60.0 / ctx.bpm) : p.rbTime->load() * 0.001f;
    s.steps.numSteps = (int) std::lround (p.trSteps->load());
    for (size_t k = 0; k < (size_t) HiveBlock::kMaxSteps; ++k)
    {
        s.steps.level[k] = pct (p.trLevels[k]);
        s.steps.move[k] = (int) p.trMoves[k]->load();
    }
    s.gate = 1.0f - 0.95f * pct (p.trChop);
    s.fromDry = on (p.trDry);
    // SYNC while the host plays: the steps follow the song grid instead of restarting on every note
    s.hostStep = on (p.rbSync) && ctx.ppq.has_value() ? *ctx.ppq / divBeats : -1.0;

    const auto mangle = HiveBlock::mangleFor (pct (p.hvMangle));
    s.anger = mangle.anger;
    s.frenzy = mangle.frenzy;
    s.buzz = mangle.buzz;
    s.raw = on (p.rbRaw);
    s.detuneCents = p.rbDetune->load();
    s.mix = pct (p.rbMix);

    hive.setParams (s);
    hive.process (audio, numChannels, numSamples);
    meters.trailStep.store (hive.getCurrentStep(), std::memory_order_relaxed);
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
Chain::Layout SwarmnessAudioProcessor::getRequestedLayout() const noexcept
{
    std::array<float, Chain::numBlocks> slots {};
    Chain::Layout l;
    for (size_t b = 0; b < slots.size(); ++b)
    {
        slots[b] = p.chainSlots[b]->load();
        l.lanes[b] = juce::jlimit (0, 2, juce::roundToInt (p.chainLanes[b]->load()));
    }
    l.order = Chain::orderFromSlots (slots);
    return l;
}

void SwarmnessAudioProcessor::setChainLayout (const Chain::Layout& layout)
{
    const auto slots = Chain::slotsForOrder (layout.order);
    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            const float norm = param->convertTo0to1 (value);
            if (std::abs (param->getValue() - norm) > 1.0e-6f)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (norm);
                param->endChangeGesture();
            }
        }
    };
    for (int b = 0; b < Chain::numBlocks; ++b)
    {
        set (Chain::slotIds[b], slots[(size_t) b]);
        set (Chain::laneIds[b], (float) layout.lanes[(size_t) b]);
    }
}

void SwarmnessAudioProcessor::setChainOrder (const Chain::Order& order)
{
    auto l = getRequestedLayout();
    l.order = order;
    setChainLayout (l);
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
juce::String SwarmnessAudioProcessor::loadNamModel (const juce::File& file)
{
    if (! file.existsAsFile())
        return "Can't find \"" + file.getFileName() + "\"";
    if (file.getSize() > 64 * 1024 * 1024)
        return "\"" + file.getFileName() + "\" is too big for a NAM capture";
    std::string error;
    auto model = NamModel::load (file.loadFileAsString().toStdString(), error);
    if (model == nullptr)
        return "\"" + file.getFileName() + "\": " + juce::String (error.empty() ? "not a NAM capture" : error);

    // name from the capture's metadata when it has one
    juce::String name = file.getFileNameWithoutExtension();
    if (auto json = juce::JSON::parse (file); json.isObject())
    {
        const auto meta = json["metadata"];
        const auto metaName = meta["name"].toString();
        if (metaName.isNotEmpty())
            name = metaName;
    }
    const auto desc = name + "  -  " + juce::String (model->getArchitecture()) + ", "
                    + juce::String (model->getSampleRate() / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";
    amp.setNamModel (std::move (model));

    const juce::ScopedLock sl (irInfoLock);
    namFile = file;
    namDescription = desc;
    return {};
}

void SwarmnessAudioProcessor::clearNamModel()
{
    amp.setNamModel (nullptr);
    const juce::ScopedLock sl (irInfoLock);
    namFile = juce::File();
    namDescription.clear();
}

juce::File SwarmnessAudioProcessor::getNamModelFile() const
{
    const juce::ScopedLock sl (irInfoLock);
    return namFile;
}

juce::String SwarmnessAudioProcessor::getNamModelDescription() const
{
    const juce::ScopedLock sl (irInfoLock);
    return namDescription;
}

juce::String SwarmnessAudioProcessor::loadCabIR (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Can't read \"" + file.getFileName() + "\" (WAV / AIFF / FLAC / OGG)";
    // cabinet IRs are short: up to 1 s (a longer file is a room - the rest is cut off)
    const int length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1.0));
    if (length < 16 || reader->sampleRate <= 0.0)
        return "\"" + file.getFileName() + "\" is too short";

    juce::AudioBuffer<float> ir (1, length);
    if (reader->numChannels > 1)
    {
        juce::AudioBuffer<float> both (2, length);
        reader->read (&both, 0, length, 0, true, true);
        ir.copyFrom (0, 0, both, 0, 0, length);
        ir.addFrom (0, 0, both, 1, 0, length);
        ir.applyGain (0.5f);
    }
    else
    {
        reader->read (&ir, 0, length, 0, true, false);
    }
    CabBlock::levelIR (ir, reader->sampleRate);
    const auto desc = file.getFileNameWithoutExtension() + "  -  " + juce::String (juce::roundToInt ((double) length / reader->sampleRate * 1000.0)) + " ms";
    cab.setUserIR (std::move (ir), reader->sampleRate);

    const juce::ScopedLock sl (irInfoLock);
    cabIRFile = file;
    cabIRDescription = desc;
    return {};
}

void SwarmnessAudioProcessor::clearCabIR()
{
    cab.clearUserIR();
    const juce::ScopedLock sl (irInfoLock);
    cabIRFile = juce::File();
    cabIRDescription.clear();
}

juce::File SwarmnessAudioProcessor::getCabIRFile() const
{
    const juce::ScopedLock sl (irInfoLock);
    return cabIRFile;
}

juce::String SwarmnessAudioProcessor::getCabIRDescription() const
{
    const juce::ScopedLock sl (irInfoLock);
    return cabIRDescription;
}

void SwarmnessAudioProcessor::updateCabModel (bool force)
{
    // the IR is rebuilt for the modelled type (IR mode keeps the last model for its cross-fade)
    int type = juce::jlimit (0, (int) CabBlock::numTypes - 1, (int) p.cabType->load());
    if (type == CabBlock::ir)
        type = cabModelKey >= 0 ? cabModelKey / 10000 : CabBlock::modern4x12;
    const int mic = juce::roundToInt (p.cabMic->load());
    const int dist = juce::roundToInt (p.cabDist->load());
    const int key = type * 10000 + mic * 100 + dist;
    const double rate = cab.getSampleRate();
    const juce::ScopedLock sl (cabModelLock);
    if (! force && key == cabModelKey && rate == cabModelRate)
        return;
    cabModelKey = key;
    cabModelRate = rate;
    cab.setModelIR (CabBlock::designIR (type, (float) mic * 0.01f, (float) dist * 0.01f, rate), rate);
}

//==============================================================================
SwarmnessAudioProcessor::~SwarmnessAudioProcessor()
{
    if (housekeeper != nullptr)
        housekeeper->stopThread (4000);
    apvts.removeParameterListener (ParamIDs::scene, this);
    cancelPendingUpdate();
}

void SwarmnessAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID != ParamIDs::scene || restoringState)
        return;
    pendingScene.store (juce::roundToInt (newValue));
    if (juce::MessageManager::existsAndIsCurrentThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();   // from the audio thread (automation / MIDI): switch on the message thread
}

void SwarmnessAudioProcessor::handleAsyncUpdate()
{
    if (presetManager != nullptr)
        presetManager->selectScene (pendingScene.load());
}

int SwarmnessAudioProcessor::indexOfParam (const juce::String& paramID) const noexcept
{
    for (int i = 0; i < learnableParams.size(); ++i)
        if (learnableParams.getUnchecked (i)->paramID == paramID)
            return i;
    return -1;
}

void SwarmnessAudioProcessor::startMidiLearn (const juce::String& paramID, int value) noexcept
{
    midiLearnValue.store (value);
    midiLearnParam.store (indexOfParam (paramID));
}

juce::String SwarmnessAudioProcessor::getMidiLearnParam() const
{
    const int i = midiLearnParam.load();
    return juce::isPositiveAndBelow (i, learnableParams.size()) ? learnableParams.getUnchecked (i)->paramID : juce::String();
}

void SwarmnessAudioProcessor::clearMidiBindings (const juce::String& paramID, int value) noexcept
{
    const int index = indexOfParam (paramID);
    for (auto& b : midiBindings)
        if (index >= 0 && b.param.load() == index && b.value.load() == value)
        {
            b.kind.store (0);
            b.param.store (-1);
        }
}

juce::String SwarmnessAudioProcessor::describeMidiBinding (const juce::String& paramID, int value) const
{
    const int index = indexOfParam (paramID);
    juce::StringArray parts;
    for (const auto& b : midiBindings)
    {
        if (index < 0 || b.param.load() != index || b.value.load() != value)
            continue;
        const int n = b.number.load();
        switch ((MidiKind) b.kind.load())
        {
            case MidiKind::cc:   parts.add ("CC " + juce::String (n)); break;
            case MidiKind::note: parts.add ("Note " + juce::MidiMessage::getMidiNoteName (n, true, true, 3)); break;
            case MidiKind::none: break;
        }
    }
    return parts.joinIntoString (", ");
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
        const int ccValue = isCC ? msg.getControllerValue() : (msg.isNoteOn() ? 127 : 0);
        const bool pressed = isCC ? ccValue >= 64 : msg.isNoteOn();

        // Learn: the first CC / note-on after "MIDI Learn" becomes a binding (added to any others)
        const int learn = midiLearnParam.load();
        if (learn >= 0 && (isCC || msg.isNoteOn()))
        {
            const int learnValue = midiLearnValue.load();
            MidiBinding* slot = nullptr;
            for (auto& b : midiBindings)
                if (b.param.load() == learn && b.kind.load() == kind && b.number.load() == number && b.value.load() == learnValue)
                    slot = &b;                                    // already bound
            for (auto& b : midiBindings)
                if (slot == nullptr && b.param.load() < 0)
                    slot = &b;
            if (slot != nullptr)
            {
                slot->kind.store (kind);
                slot->number.store (number);
                slot->down.store (pressed);                        // the learning press does not also switch
                slot->value.store (learnValue);
                slot->param.store (learn);
            }
            midiLearnParam.store (-1);
            continue;
        }

        for (auto& b : midiBindings)
        {
            const int index = b.param.load();
            if (index < 0 || b.kind.load() != kind || b.number.load() != number)
                continue;
            if (auto* param = learnableParams[index])
            {
                const bool wasDown = b.down.exchange (pressed);
                if (const int fixed = b.value.load(); fixed >= 0)
                {
                    if (pressed && ! wasDown)
                        param->setValueNotifyingHost (param->convertTo0to1 ((float) fixed));   // e.g. "scene C"
                    continue;
                }
                const bool continuous = dynamic_cast<juce::AudioParameterBool*> (param) == nullptr
                                     && dynamic_cast<juce::AudioParameterChoice*> (param) == nullptr;
                if (continuous ? isCC : wasDown != pressed)
                    applyMidi (*param, isCC, ccValue, pressed);
            }
        }
    }
}

void SwarmnessAudioProcessor::applyMidi (juce::RangedAudioParameter& param, bool isCC, int ccValue, bool pressed)
{
    if (dynamic_cast<juce::AudioParameterBool*> (&param) != nullptr)
    {
        const auto& id = param.paramID;
        const bool footswitch = id == ParamIDs::oct1 || id == ParamIDs::oct2 || id == ParamIDs::magicHold;
        const bool momentary = apvts.getRawParameterValue (ParamIDs::switchMode)->load() < 0.5f;
        if (footswitch && momentary)
            param.setValueNotifyingHost (pressed ? 1.0f : 0.0f);                           // held = on
        else if (pressed)
            param.setValueNotifyingHost (param.getValue() >= 0.5f ? 0.0f : 1.0f);          // toggle per press
        return;
    }
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
    {
        if (pressed)
        {
            const int n = choice->choices.size();
            param.setValueNotifyingHost (param.convertTo0to1 ((float) ((choice->getIndex() + 1) % juce::jmax (1, n))));
        }
        return;
    }
    if (isCC)
        param.setValueNotifyingHost ((float) ccValue / 127.0f);
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
    state.setProperty ("uiMini", uiMini.load(), nullptr);
    state.setProperty ("reverbIR", getReverbIRFile().getFullPathName(), nullptr);
    state.setProperty ("namModel", getNamModelFile().getFullPathName(), nullptr);
    state.setProperty ("cabIR", getCabIRFile().getFullPathName(), nullptr);
    juce::StringArray bindings;
    for (const auto& b : midiBindings)
        if (const int index = b.param.load(); index >= 0 && b.kind.load() != 0)
            bindings.add (learnableParams[index]->paramID + ":" + juce::String (b.kind.load()) + ":" + juce::String (b.number.load())
                          + (b.value.load() >= 0 ? ":" + juce::String (b.value.load()) : juce::String()));
    state.setProperty ("midiBindings", bindings.joinIntoString (";"), nullptr);
    state.setProperty ("scenes", juce::JSON::toString (presetManager->scenesToVar(), true), nullptr);

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
            restoringState = true;   // the scene parameter comes back with the state; its scenes are restored below
            cancelPendingUpdate();   // a scene switch queued before the restore must not land on top of it
            const auto scenesJson = tree.getProperty ("scenes").toString();
            tree.removeProperty ("scenes", nullptr);
            uiScale = juce::jlimit (0.7f, 2.0f, (float) tree.getProperty ("uiScale", 1.0f));
            uiMini = (bool) tree.getProperty ("uiMini", false);
            // MIDI bindings ("paramID:kind:number;..."); up to beta.24 only the footswitches ("midi0".."midi3")
            for (auto& b : midiBindings)
            {
                b.param.store (-1);
                b.kind.store (0);
                b.down.store (false);
            }
            size_t next = 0;
            auto addBinding = [&] (const juce::String& paramID, int kind, int number, int value = -1)
            {
                const int index = indexOfParam (paramID);
                if (index < 0 || kind <= 0 || kind > 2 || number < 0 || next >= midiBindings.size())
                    return;
                auto& b = midiBindings[next++];
                b.kind.store (kind);
                b.number.store (number);
                b.value.store (value);
                b.param.store (index);
            };
            for (const auto& entry : juce::StringArray::fromTokens (tree.getProperty ("midiBindings").toString(), ";", ""))
            {
                const auto parts = juce::StringArray::fromTokens (entry, ":", "");
                if (parts.size() >= 3)
                    addBinding (parts[0], parts[1].getIntValue(), parts[2].getIntValue(), parts.size() > 3 ? parts[3].getIntValue() : -1);
            }
            static const char* legacyTargets[] { ParamIDs::oct1, ParamIDs::oct2, ParamIDs::magicHold, ParamIDs::bypass };
            for (int t = 0; t < 4; ++t)
            {
                const auto v = tree.getProperty ("midi" + juce::String (t)).toString();
                if (v.isNotEmpty())
                    addBinding (legacyTargets[t], v.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                v.fromFirstOccurrenceOf (":", false, false).getIntValue());
            }
            tree.removeProperty ("midiBindings", nullptr);
            for (int t = 0; t < 4; ++t)
                tree.removeProperty ("midi" + juce::String (t), nullptr);

            // Sessions from older betas: STING + HIVE became one HIVE block (DIVE, separate RAW / DETUNE).
            std::map<juce::String, float> legacyValues;
            for (const auto& child : tree)
                if (child.hasType ("PARAM"))
                    legacyValues[child.getProperty ("id").toString()] = (float) child.getProperty ("value", 0.0f);
            const auto beforeMigration = legacyValues;
            PresetManager::migrateLegacyValues (legacyValues);

            // Sessions from before the chain: SMOKE "POST" becomes SMOKE placed after SWARM.
            const auto legacyPost = tree.getChildWithProperty ("id", ParamIDs::fuzzPostLegacy);
            const bool migrateSmoke = legacyPost.isValid() && (float) legacyPost.getProperty ("value", 0.0f) > 0.5f
                                      && ! tree.getChildWithProperty ("id", Chain::slotIds[Chain::smoke]).isValid();

            apvts.replaceState (tree);
            // replaceState skips a parameter whose stored value equals its current (rounded) value, so a
            // switch left at e.g. 0.44 would stay there: put every parameter exactly on the stored value
            for (const auto& child : tree)
                if (child.hasType ("PARAM"))
                    if (auto* param = apvts.getParameter (child.getProperty ("id").toString()))
                    {
                        const float target = param->convertTo0to1 ((float) child.getProperty ("value", 0.0f));
                        if (std::abs (param->getValue() - target) > 1.0e-6f)
                            param->setValueNotifyingHost (target);
                    }

            if (migrateSmoke)
                if (auto* slot = apvts.getParameter (Chain::slotIds[Chain::smoke]))
                    slot->setValueNotifyingHost (slot->convertTo0to1 ((float) Chain::legacyPostSmokeSlot));

            for (const auto& [paramId, value] : legacyValues)
                if (auto it = beforeMigration.find (paramId); it == beforeMigration.end() || std::abs (it->second - value) > 1.0e-6f)
                    if (auto* param = apvts.getParameter (paramId))
                        param->setValueNotifyingHost (param->convertTo0to1 (value));

            const auto irPath = tree.getProperty ("reverbIR").toString();
            if (irPath.isNotEmpty() && juce::File::isAbsolutePath (irPath) && juce::File (irPath).existsAsFile())
                loadReverbIR (juce::File (irPath));
            else
                clearReverbIR();
            const auto namPath = tree.getProperty ("namModel").toString();
            if (namPath.isNotEmpty() && juce::File::isAbsolutePath (namPath) && juce::File (namPath).existsAsFile())
                loadNamModel (juce::File (namPath));
            else
                clearNamModel();
            const auto cabPath = tree.getProperty ("cabIR").toString();
            if (cabPath.isNotEmpty() && juce::File::isAbsolutePath (cabPath) && juce::File (cabPath).existsAsFile())
                loadCabIR (juce::File (cabPath));
            else
                clearCabIR();

            presetManager->scenesFromVar (juce::JSON::parse (scenesJson),
                                          juce::roundToInt (apvts.getRawParameterValue (ParamIDs::scene)->load()));
            presetManager->restoreFromState (tree.getProperty ("presetName").toString());
            pendingScene.store (presetManager->getCurrentScene());
            restoringState = false;

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
