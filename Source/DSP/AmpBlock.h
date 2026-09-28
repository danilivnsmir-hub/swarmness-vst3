#pragma once

#include <JuceHeader.h>
#include "NamRunner.h"
#include "NoiseGate.h"
#include "AmpCircuit.h"

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <memory>
#include <vector>

/**
 * AMP - a guitar amplifier: three channels of circuit-level amp models, or a Neural Amp Modeler capture.
 *
 * CLEAN / CRUNCH / LEAD each sit between two reference amps (see DSP/AmpCircuit.h), and CHARACTER
 * morphs the circuit between them - the middle is an amp of its own:
 *    CLEAN   CHROME (solid-state jazz clean)       <-> BLACKFACE (American clean / breakup)
 *    CRUNCH  BRIT   (British crunch)               <-> CITRUS    (thick British fuzz-crunch)
 *    LEAD    STEEL  (tight American high gain)     <-> SLUDGE    (loose modern high gain)
 * NAM runs a loaded .nam capture instead, with its own knobs (INPUT, post EQ, OUTPUT; 5 = neutral).
 *
 * gate -> [8x] input (volts) -> preamp stages / GAIN pot / tone stack -> MASTER -> power amp
 * with feedback, sag and the speaker load -> [1x] -> level
 *
 * The oversampling is IIR (minimum phase, like the analog circuit) and not reported as latency.
 * Like a real amp it is mono: the circuit runs on the mono sum (stereo effects go after it).
 */
class AmpBlock
{
public:
    enum Channel : int { clean = 0, crunch, lead, nam, numChannels };

    struct Settings
    {
        bool on = false;
        int channel = crunch;
        float character = 0.5f;               // 0 = first reference, 1 = second
        float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.5f;   // 0..1 (knob 0..10)
        float presence = 0.5f, depth = 0.5f, master = 0.5f;
        float gate = 0.0f;                    // 0 = off
        float levelDb = 0.0f;
        // NAM mode's own knobs, 0..1 (knob 0..10), 0.5 = neutral
        float namInput = 0.5f, namBass = 0.5f, namMid = 0.5f, namTreble = 0.5f;
        float namPresence = 0.5f, namDepth = 0.5f, namOutput = 0.5f;
    };

    /** Model names: reference A, the in-between amp, reference B. */
    /** One amp per channel: CLEAN = BLACKFACE, CRUNCH = BRIT, LEAD = STEEL (which of the channel's two
        fitted reference circuits it uses). */
    static float channelSide (int channel) noexcept { return channel == clean ? 1.0f : 0.0f; }
    static const char* channelModel (int channel) noexcept
    {
        return channel >= 0 && channel < 3 ? referenceName (channel, (int) channelSide (channel)) : "NAM";
    }

    static const char* modelName (int channel, float character) noexcept
    {
        static const char* names[3][3] { { "CHROME", "GLASSHOUSE", "BLACKFACE" },
                                         { "BRIT", "MARMALADE", "CITRUS" },
                                         { "STEEL", "IRONHIVE", "SLUDGE" } };
        if (channel < 0 || channel > 2) return "NAM";
        return names[channel][character < 0.3f ? 0 : (character > 0.7f ? 2 : 1)];
    }
    static const char* referenceName (int channel, int side) noexcept
    {
        static const char* names[3][2] { { "CHROME", "BLACKFACE" }, { "BRIT", "CITRUS" }, { "STEEL", "SLUDGE" } };
        return channel >= 0 && channel < 3 ? names[channel][side & 1] : "";
    }

    //==============================================================================
    void prepare (double sampleRate, int maxBlockSize)
    {
        (void) ampsim::tables();   // built once per process
        (void) ampsim::gridCurve();
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        // the amp models run at ~350-400 kHz (hard-clipping stages alias less), NAM at ~176-192 kHz
        osLog2 = sampleRate < 60000.0 ? 3 : (sampleRate < 120000.0 ? 2 : 1);
        osFactor = 1 << osLog2;
        fsOs = fs * osFactor;
        auto make = [this] (int log2, int channels) -> std::unique_ptr<juce::dsp::Oversampling<float>>
        {
            if (log2 <= 0) return nullptr;
            auto o = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, (size_t) log2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            o->initProcessing ((size_t) maxBlock);
            return o;
        };
        oversampler = make (osLog2, 1);
        monoBuffer.setSize (1, maxBlock, false, false, true);
        dryCopy.setSize (2, maxBlock, false, false, true);
        namCopy.setSize (2, maxBlock, false, false, true);

        namRunner.prepare (sampleRate, maxBlock);
        gate.prepare (sampleRate, maxBlock);
        reset();
    }

    void reset()
    {
        for (auto& c : ch)
            c = {};
        for (auto& e : namEq) e.reset();
        if (oversampler != nullptr) oversampler->reset();
        gate.reset();
        firstUpdate = true;
        coeffsDirty = true;
        blendReady = false;
        switchGain = 1.0f;
        activeChannel = juce::jlimit (0, 2, settings.channel == nam ? activeChannel : settings.channel);
        namRunner.reset();
        onGain.reset (fs, 0.02);
        onGain.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        namMix.reset (fs, 0.03);
        namMix.setCurrentAndTargetValue (settings.channel == nam ? 1.0f : 0.0f);
        level.reset (fs, 0.03);
        level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
    }

    void setParams (const Settings& s) noexcept { settings = s; }

    //==============================================================================
    /** Hands over a NAM capture (message thread; nullptr clears). The old one is freed on the next call. */
    void setNamModel (std::unique_ptr<NamModel> model) { namRunner.setModel (std::move (model)); }
    bool hasNamModel() const noexcept { return namRunner.hasModel(); }
    /** A2 captures: Full (1) or Lite (0). Off the audio thread (the housekeeping thread calls it). */
    void setNamSize (double size01) { namRunner.setSize (size01); }
    /** Frees a model the audio thread swapped out (message thread). */
    void releaseRetired() { namRunner.releaseRetired(); }

    //==============================================================================
    void process (float* const* audio, int numCh, int numSamples) noexcept
    {
        numCh = juce::jmin (numCh, 2);
        namRunner.pickUp();

        onGain.setTargetValue (settings.on ? 1.0f : 0.0f);
        if (! settings.on && ! onGain.isSmoothing())
        {
            onGain.setCurrentAndTargetValue (0.0f);
            return;   // off: the signal passes untouched
        }

        for (int c = 0; c < numCh; ++c)
            dryCopy.copyFrom (c, 0, audio[c], numSamples);

        const bool useNam = settings.channel == nam && namRunner.isActive();
        namMix.setTargetValue (useNam ? 1.0f : 0.0f);
        const bool runNam = useNam || namMix.isSmoothing();
        const bool runModel = ! useNam || namMix.isSmoothing();

        // the gate: keyed from the input, applied to the input and again to the output below
        const float* gateCurve = gate.compute (audio, numCh, numSamples, settings.gate);
        if (gateCurve != nullptr)
            for (int c = 0; c < numCh; ++c)
                juce::FloatVectorOperations::multiply (audio[c], gateCurve, numSamples);
        if (runNam)
            for (int c = 0; c < numCh; ++c)
                namCopy.copyFrom (c, 0, audio[c], numSamples);

        if (runModel)
            processModel (audio, numCh, numSamples);
        if (runNam)
        {
            float* namAudio[2] { namCopy.getWritePointer (0), namCopy.getWritePointer (1) };
            processNam (namAudio, numCh, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const float m = namMix.getNextValue();
                for (int c = 0; c < numCh; ++c)
                    audio[c][i] = runModel ? audio[c][i] + m * (namAudio[c][i] - audio[c][i]) : namAudio[c][i];
            }
        }

        level.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue() * (gateCurve != nullptr ? gateCurve[i] : 1.0f), on = onGain.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                const float wet = audio[c][i] * g;
                audio[c][i] = dryCopy.getSample (c, i) + on * (wet - dryCopy.getSample (c, i));
            }
        }
    }

    //==============================================================================
    /** FMV tone stack magnitude of a model (for tests and the editor), knob positions 0..1. */
    static float toneStackDb (int channel, float character, float bass, float mid, float treble, float hz)
    {
        const auto a = ampsim::reference (juce::jlimit (0, 2, channel), 0), b = ampsim::reference (juce::jlimit (0, 2, channel), 1);
        auto geo = [character] (float va, float vb) { return (double) va * std::pow ((double) vb / va, (double) character); };
        double bb[4], aa[4];
        ampsim::toneStackAnalog (geo (a.R1, b.R1), geo (a.R2, b.R2), geo (a.R3, b.R3), geo (a.R4, b.R4),
                                 geo (a.C1, b.C1), geo (a.C2, b.C2), geo (a.C3, b.C3), bass, mid, treble, bb, aa);
        return (float) (20.0 * std::log10 (std::abs (ampsim::evalAnalog (bb, aa, hz))));
    }

    /** Test / tuning probe: peak volts at each stage output [0..3], the tone stack out [4], the phase
        inverter drive [5], output tube current [6], supply [7] (min). Reset with probeReset(). */
    std::array<float, 8> probe {};
    bool probing = false;
    void probeReset() noexcept { probe.fill (0.0f); probe[7] = 1.0f; }

private:
    //==============================================================================
    static constexpr float kInputVolts = 2.5f;      // full scale = 2.5 V from the guitar
    static constexpr int subBlock = 16;             // coefficient refresh (host-rate samples)

    static inline float tanhR (float x) noexcept
    {
        // rational tanh, accurate to ~1e-4 inside +-4.5, clamped outside
        x = juce::jlimit (-4.5f, 4.5f, x);
        const float x2 = x * x;
        return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
    }

    // The morphed circuit: every value interpolated between the channel's two references
    struct StageCoeffs
    {
        bool present = false, coupled = false, cath = false, shelf = false;
        float Rs = 68e3f, Rg = 1e6f, dtOverC = 0.0f, thr = 1000.0f, cathA = 1.0f, divShelfA = 1.0f, div = 1.0f;
        float gridK = 1.0f, c2 = 1.0f, invC2 = 1.0f, charge = 0.0f;   // precomputed for the grid solve
    };
    struct Coeffs
    {
        std::array<StageCoeffs, 4> st;
        int gainPotAfter = 0;
        float gridKg = 3.0e-4f, inGain = kInputVolts;
        bool stackEarly = false;
        float alpha = 0.1f, brightA = 1.0f, cf = 0.0f, stackMakeup = 1.0f, masterA = 0.1f;
        float piMax = 1.6f, invPiMax = 1.0f / 1.6f, bias = 0.55f, hard = 0.0f, satKnee = 2.0f, nfbIn = 3.0f, beta = 1.0f, idle2 = 0.5f;
        float sag = 0.3f, sagCoeff = 0.0f, presAmt = 0.0f, depthAmt = 0.0f, coilA = 1.0f, paRefInv = 1.0f, out = 1.0f;
        float paThr = 0.5f, paDtOverC = 0.0f;
    };

    struct StageState { float vC = 0.0f; ampsim::OnePole cath, lp, divShelf; };
    struct ChannelState
    {
        std::array<StageState, 4> st;
        ampsim::OnePole bright;
        ampsim::ToneStack stack;
        ampsim::Biquad voice, spk;
        ampsim::OnePole coil, pres, depth, xfHp, xfLp, loop1, loop2;
        float vCa = 0.0f, vCb = 0.0f, ePrev = 0.0f, slope = 1.0f, sagEnv = 0.0f, supply = 1.0f;
    };

    static float lerp (float a, float b, float x) noexcept { return a + x * (b - a); }
    static float geo (float a, float b, float x) noexcept { return a * std::pow (b / a, x); }
    /** A log (A-taper) pot: 0 = off, noon ~ -24 dB, full = 0 dB. */
    static float audioTaper (float v) noexcept { return 1.0e-4f + (std::exp (5.5f * v) - 1.0f) / (std::exp (5.5f) - 1.0f); }

    void updateCoefficients() noexcept
    {
        const int wanted = settings.channel == nam ? activeChannel : juce::jlimit (0, 2, settings.channel);
        const float knobs[8] { settings.character, settings.gain, settings.bass, settings.mid, settings.treble,
                               settings.presence, settings.depth, settings.master };
        const float k = 1.0f - std::exp (-(float) subBlock / (0.03f * (float) fs));   // ~30 ms glide
        if (firstUpdate)
        {
            activeChannel = wanted;
            for (int i = 0; i < 8; ++i) knobCur[(size_t) i] = knobs[i];
            firstUpdate = false;
        }
        else
        {
            for (int i = 0; i < 8; ++i) knobCur[(size_t) i] += k * (knobs[i] - knobCur[(size_t) i]);
        }
        // nothing moving: the coefficients stay
        bool settled = wanted == activeChannel && switchGain >= 1.0f && ! coeffsDirty;
        for (int i = 0; i < 8 && settled; ++i)
            settled = std::abs (knobs[i] - knobCur[(size_t) i]) < 1.0e-5f;
        if (settled && lastKnobs == knobCur)
        {
            if (! blendReady || blendChannel != activeChannel || blendMix != tableMix)
                buildBlend();
            return;
        }
        blendReady = false;
        coeffsDirty = false;
        // a channel switch: fade out (~6 ms), switch the circuit, fade back in
        const float fadeStep = (float) subBlock / (0.006f * (float) fs);
        if (wanted != activeChannel)
        {
            switchGain = juce::jmax (0.0f, switchGain - fadeStep);
            if (switchGain <= 0.0f)
            {
                activeChannel = wanted;
                for (auto& c : ch) c = {};
                knobCur[0] = knobs[0];
            }
        }
        else
        {
            switchGain = juce::jmin (1.0f, switchGain + fadeStep);
        }

        lastKnobs = knobCur;
        const float x = juce::jlimit (0.0f, 1.0f, knobCur[0]);
        tableMix = x;
        const auto A = ampsim::reference (activeChannel, 0), B = ampsim::reference (activeChannel, 1);
        const auto& tbl = ampsim::tables();
        tableA = &tbl[(size_t) (activeChannel * 2)];
        tableB = &tbl[(size_t) (activeChannel * 2 + 1)];
        const double dt = 1.0 / fsOs;

        co.gridKg = geo (A.gridKg, B.gridKg, x);
        co.inGain = kInputVolts * juce::Decibels::decibelsToGain (lerp (A.inDb, B.inDb, x));
        for (size_t s = 0; s < 4; ++s)
        {
            const auto& a = A.st[s];
            const auto& b = B.st[s];
            auto& c = co.st[s];
            c.present = a.type != 0 || b.type != 0;
            if (! c.present) continue;
            c.Rs = geo (a.Rs, b.Rs, x);
            c.Rg = geo (a.Rg, b.Rg, x);
            const float C = (a.C > 0.0f && b.C > 0.0f) ? geo (a.C, b.C, x) : lerp (a.C, b.C, x);
            c.coupled = C > 0.0f;
            c.dtOverC = c.coupled ? (float) (dt / C) : 0.0f;
            c.thr = lerp ((*tableA)[s].thr, (*tableB)[s].thr, x);
            c.cathA = juce::Decibels::decibelsToGain (lerp (a.cathDb, b.cathDb, x));
            c.div = lerp (a.div, b.div, x);
            c.divShelfA = juce::Decibels::decibelsToGain (lerp (a.divShelfDb, b.divShelfDb, x));
            c.cath = std::abs (c.cathA - 1.0f) > 1.0e-4f;
            c.shelf = std::abs (c.divShelfA - 1.0f) > 1.0e-4f;
            c.gridK = c.Rg / (c.Rs + c.Rg);
            const float cc = c.Rs * c.gridK * co.gridKg;
            c.c2 = cc * cc;
            c.invC2 = 1.0f / c.c2;
            c.charge = c.dtOverC / c.Rs;
            for (auto& cs : ch)
            {
                cs.st[s].cath.set (geo (a.cathHz, b.cathHz, x), fsOs);
                cs.st[s].lp.set (geo (a.lpHz, b.lpHz, x), fsOs);
                cs.st[s].divShelf.set (geo (a.divShelfHz, b.divShelfHz, x), fsOs);
            }
        }
        co.gainPotAfter = A.gainPotAfter;
        co.stackEarly = A.stackEarly;
        const float gain = knobCur[1], bass = knobCur[2], mid = knobCur[3], treble = knobCur[4];
        const float presence = knobCur[5], depth = knobCur[6], master = knobCur[7];
        co.alpha = audioTaper (gain);
        co.brightA = juce::Decibels::decibelsToGain (lerp (A.brightDb, B.brightDb, x) * (1.0f - gain) * (1.0f - gain));
        co.cf = lerp (A.cf, B.cf, x);
        co.masterA = audioTaper (master);

        double bq[4], aq[4], b0[4], a0[4];
        const double R1 = geo (A.R1, B.R1, x), R2 = geo (A.R2, B.R2, x), R3 = geo (A.R3, B.R3, x), R4 = geo (A.R4, B.R4, x);
        const double C1 = geo (A.C1, B.C1, x), C2 = geo (A.C2, B.C2, x), C3 = geo (A.C3, B.C3, x);
        ampsim::toneStackAnalog (R1, R2, R3, R4, C1, C2, C3, bass, mid, treble, bq, aq);
        ampsim::toneStackAnalog (R1, R2, R3, R4, C1, C2, C3, 0.5f, 0.5f, 0.5f, b0, a0);
        // the stack's loss at noon is made up (like the gain stage after it in the real amp)
        co.stackMakeup = (float) (1.0 / juce::jmax (1.0e-3, std::abs (ampsim::evalAnalog (b0, a0, 700.0))));

        co.piMax = lerp (A.piMax, B.piMax, x);
        co.invPiMax = 1.0f / co.piMax;
        co.bias = lerp (A.bias, B.bias, x);
        co.hard = lerp (A.hard, B.hard, x);
        co.satKnee = lerp (A.satKnee, B.satKnee, x);
        const float nfb = geo (A.nfb, B.nfb, x);
        const float g0 = 3.0f * std::sqrt (juce::jmax (0.02f, 1.0f - co.bias));   // small-signal gain of the pair
        co.beta = nfb / g0;
        co.nfbIn = 1.0f + nfb;
        co.idle2 = 2.0f * std::pow (juce::jmax (0.0f, 1.0f - co.bias), 1.5f);
        co.sag = lerp (A.sag, B.sag, x);
        co.sagCoeff = 1.0f - std::exp (-1.0f / (lerp (A.sagMs, B.sagMs, x) * 0.001f * (float) fsOs));
        co.presAmt = lerp (A.presenceMax, B.presenceMax, x) * presence;
        co.depthAmt = lerp (A.depthMax, B.depthMax, x) * depth;
        co.coilA = juce::Decibels::decibelsToGain (lerp (A.coilDb, B.coilDb, x));
        co.paRefInv = 1.0f / lerp (A.paRef, B.paRef, x);
        co.paThr = co.bias + co.hard * 1.0e6f;
        co.paDtOverC = (float) (dt / 0.02);   // grid leak x coupling cap = 20 ms (normalised units)
        // the in-between amp is its own circuit: its level is matched to the two references too
        static constexpr float middleTrimDb[3] { 0.0f, -8.8f, -1.3f };
        co.out = juce::Decibels::decibelsToGain (lerp (A.outDb, B.outDb, x) + middleTrimDb[activeChannel] * 4.0f * x * (1.0f - x)) * 0.18f;

        for (auto& cs : ch)
        {
            for (auto& ts : { &cs.stack })
            {
                const double z[3] { ts->z[0], ts->z[1], ts->z[2] };
                ts->set (bq, aq, fsOs);
                ts->z[0] = z[0]; ts->z[1] = z[1]; ts->z[2] = z[2];
            }
            cs.bright.set (geo (A.brightHz, B.brightHz, x), fsOs);
            cs.voice.peak (geo (A.voiceHz, B.voiceHz, x), lerp (A.voiceQ, B.voiceQ, x), lerp (A.voiceDb, B.voiceDb, x), fsOs);
            cs.spk.peak (geo (A.spkHz, B.spkHz, x), lerp (A.spkQ, B.spkQ, x), lerp (A.spkDb, B.spkDb, x), fsOs);
            cs.coil.set (geo (A.coilHz, B.coilHz, x), fsOs);
            cs.pres.set (geo (A.presenceHz, B.presenceHz, x), fsOs);
            cs.depth.set (geo (A.depthHz, B.depthHz, x), fsOs);
            cs.xfHp.set (geo (A.xfHp, B.xfHp, x), fsOs);
            cs.xfLp.set (geo (A.xfLp, B.xfLp, x), fsOs);
            // the output transformer's top end and the compensation cap: they keep the loop stable
            cs.loop1.set (28000.0, fsOs);
            cs.loop2.set (40000.0, fsOs);
        }
    }

    inline float stageTable (size_t s, float u) const noexcept
    {
        if (blendReady)
            return blended[s] (u);
        const float ya = (*tableA)[s] (u);
        return tableMix <= 0.0f ? ya : ya + tableMix * ((*tableB)[s] (u) - ya);
    }

    /** CHARACTER at rest: the two references' curves merged into one table (one look-up per stage). */
    void buildBlend() noexcept
    {
        for (size_t s = 0; s < 4; ++s)
        {
            auto& dst = blended[s].y;
            const auto& a = (*tableA)[s].y;
            const auto& b = (*tableB)[s].y;
            for (size_t i = 0; i < dst.size(); ++i)
                dst[i] = a[i] + tableMix * (b[i] - a[i]);
        }
        blendReady = true;
        blendChannel = activeChannel;
        blendMix = tableMix;
    }

    inline float processSample (float in, ChannelState& cs) noexcept
    {
        float v = in * co.inGain;
        for (size_t s = 0; s < 4; ++s)
        {
            const auto& c = co.st[s];
            if (! c.present) continue;
            auto& ss = cs.st[s];
            // coupling cap + grid current (blocking)
            const float vin = c.coupled ? v - ss.vC : v;
            const float vg = ampsim::solveGridFast (vin, c.gridK, c.thr, c.c2, c.invC2);
            if (c.coupled)
                ss.vC += (vin - vg) * c.charge;
            // partial cathode bypass: less gain in the lows
            const float u = c.cath ? vg + (c.cathA - 1.0f) * ss.cath.lp (vg) : vg;
            float y = ss.lp.lp (stageTable (s, u));
            if ((int) s == co.gainPotAfter)
            {
                y *= co.alpha;
                y += (co.brightA - 1.0f) * cs.bright.hp (y);
            }
            if (s == 0 && co.stackEarly)
                y = cs.stack.process (y) * co.stackMakeup;
            if (probing) probe[s] = juce::jmax (probe[s], std::abs (y));
            y *= c.div;
            if (c.shelf)
                y += (c.divShelfA - 1.0f) * ss.divShelf.hp (y);
            v = y;
        }
        if (! co.stackEarly)
        {
            // cathode follower: soft limit when its grid swings above the cathode
            if (co.cf > 0.0f)
                v += co.cf * ((v > 0.0f ? 45.0f * tanhR (v / 45.0f) : v) - v);
            v = cs.voice.process (v);
            v = cs.stack.process (v) * co.stackMakeup;
        }
        else
        {
            v = cs.voice.process (v);
        }

        // ---- power amp: LTP phase inverter -> push-pull pair with grid blocking -> speaker load, NFB, sag
        const float drive = v * co.masterA * co.paRefInv;
        if (probing)
        {
            probe[4] = juce::jmax (probe[4], std::abs (v));
            probe[5] = juce::jmax (probe[5], std::abs (drive));
        }
        // The pair's output current for an error signal e (the grid caps and the supply held for this sample)
        const float imax = co.satKnee * cs.supply, invImax = 1.0f / imax;
        // PA grids: Rs = 0.15, Rg = 1 (normalised), kg = 2
        constexpr float paK = 1.0f / 1.15f, paC = 0.15f * paK * 2.0f, paC2 = paC * paC, paInvC2 = 1.0f / paC2;
        auto grid = [&] (float vs, float vC) noexcept { return ampsim::solveGridFast (vs - vC, paK, co.paThr, paC2, paInvC2); };
        auto tubeI = [&] (float vg) noexcept
        {
            const float g = juce::jmax (0.0f, vg - co.bias + 1.0f);
            const float ip = g * std::sqrt (g);                                 // Child's law
            const float soft = imax * tanhR (ip * invImax);                     // plate saturation
            return co.hard > 0.0f ? soft + co.hard * (juce::jmin (ip, imax) - soft) : soft;
        };
        struct PairState { float pv, vga, vgb, i1, i2; };
        PairState ps {};
        auto pair = [&] (float e) noexcept
        {
            ps.pv = co.piMax * tanhR (e * co.invPiMax);                         // long-tailed-pair inverter
            ps.vga = grid (ps.pv, cs.vCa);
            ps.vgb = grid (-ps.pv, cs.vCb);
            ps.i1 = tubeI (ps.vga);
            ps.i2 = tubeI (ps.vgb);
            return ps.i1 - ps.i2;
        };

        // Global feedback without a delay in the loop: every linear filter splits into an
        // instantaneous gain and its state, and e + K * pair (e) = rhs is solved by Newton
        // (the left side always rises, so it converges in a few steps at any feedback depth).
        const float cG = cs.coil.G, cA = co.coilA - 1.0f;
        const float k2a = 1.0f + cA * (1.0f - cG);                              // coil shelf: gain ...
        const float s2a = -cA * (1.0f - cG) * cs.coil.s;                        // ... and state
        const float b0 = (float) cs.spk.b0, z1 = (float) cs.spk.z1;
        const float k2 = k2a * b0, s2 = b0 * s2a + z1;                          // y = k2 * current + s2
        const float G1 = cs.loop1.G, G2 = cs.loop2.G, Gp = cs.pres.G, Gd = cs.depth.G;
        const float shape = 1.0f - co.presAmt * (1.0f - Gp) - co.depthAmt * Gd;
        const float yfK = G2 * G1, yfS = G2 * (1.0f - G1) * cs.loop1.s + (1.0f - G2) * cs.loop2.s;
        const float fbConst = co.presAmt * (1.0f - Gp) * cs.pres.s - co.depthAmt * (1.0f - Gd) * cs.depth.s;
        // fb = beta * (shape * yf + fbConst), yf = yfK * y + yfS
        const float K = co.beta * shape * yfK * k2;
        const float rhs = drive * co.nfbIn - co.beta * (shape * (yfK * s2 + yfS) + fbConst);
        // one Newton step from the last sample's solution, with the slope carried over (at 8x the
        // signal barely moves between samples), then one refinement when it moved a lot
        float e = cs.ePrev;
        float f = e + K * pair (e) - rhs;
        e -= f / (1.0f + K * cs.slope);
        const float p1 = pair (e);
        f = e + K * p1 - rhs;
        if (std::abs (f) > 1.0e-4f * (1.0f + std::abs (rhs)))
        {
            const float p2 = pair (e + 1.0e-3f);
            cs.slope = juce::jmax (0.0f, (p2 - p1) * 1.0e3f);
            e -= f / (1.0f + K * cs.slope);
            pair (e);
        }
        cs.ePrev = e;

        // commit (ps holds the solution): the grid caps, the filters, the supply
        cs.vCa += ((ps.pv - cs.vCa) - ps.vga) * (co.paDtOverC / 0.15f);
        cs.vCb += ((-ps.pv - cs.vCb) - ps.vgb) * (co.paDtOverC / 0.15f);
        const float i1 = ps.i1, i2 = ps.i2;
        float y = i1 - i2;
        if (probing)
        {
            probe[6] = juce::jmax (probe[6], std::abs (y));
            probe[7] = juce::jmin (probe[7], cs.supply);
        }
        y += cA * cs.coil.hp (y);                  // voice-coil inductance
        y = cs.spk.process (y);                    // cone resonance
        const float yf = cs.loop2.lp (cs.loop1.lp (y));
        cs.pres.hp (yf);
        cs.depth.lp (yf);
        cs.sagEnv += co.sagCoeff * ((i1 + i2 - co.idle2) - cs.sagEnv);
        cs.supply = 1.0f / (1.0f + co.sag * juce::jmax (0.0f, cs.sagEnv));
        y = cs.xfLp.lp (cs.xfHp.hp (y));
        return y * co.out;
    }

    void processModel (float* const* audio, int numCh, int numSamples) noexcept
    {
        // An amp has one input and one speaker: the circuit runs on the mono sum, both outputs get it
        // (put stereo effects after the AMP to keep them wide).
        float* mono = monoBuffer.getWritePointer (0);
        if (numCh > 1)
            for (int i = 0; i < numSamples; ++i)
                mono[i] = 0.5f * (audio[0][i] + audio[1][i]);
        else
            juce::FloatVectorOperations::copy (mono, audio[0], numSamples);

        float* os = mono;
        int nOs = numSamples;
        float* monoPtr[1] { mono };
        juce::dsp::AudioBlock<float> block (monoPtr, 1, (size_t) numSamples);
        juce::dsp::AudioBlock<float> up;
        if (oversampler != nullptr)
        {
            up = oversampler->processSamplesUp (block);
            nOs = (int) up.getNumSamples();
            os = up.getChannelPointer (0);
        }

        const int sub = subBlock * osFactor;
        auto& cs = ch[0];
        for (int start = 0; start < nOs; start += sub)
        {
            updateCoefficients();
            const int end = juce::jmin (nOs, start + sub);
            for (int i = start; i < end; ++i)
                os[i] = processSample (os[i], cs) * switchGain;
        }

        if (oversampler != nullptr)
            oversampler->processSamplesDown (block);
        for (int c = 0; c < numCh; ++c)
            juce::FloatVectorOperations::copy (audio[c], mono, numSamples);
    }

    //==============================================================================
    // Noise gate on the input (before the gain): keyed by the louder channel, with hold + hysteresis
    //==============================================================================

    /** A NAM knob (0..1) in dB: 0 at noon, +-range at the ends. */
    static float namKnobDb (float knob, float range) noexcept { return (knob - 0.5f) * 2.0f * range; }

    void processNam (float* const* audio, int numCh, int numSamples) noexcept
    {
        // its own knobs, 5 = neutral: INPUT +-18 dB into the capture, BASS..DEPTH post EQ, OUTPUT +-18 dB
        namRunner.process (audio, numCh, numSamples,
                           juce::Decibels::decibelsToGain (namKnobDb (settings.namInput, 18.0f)),
                           juce::Decibels::decibelsToGain (namKnobDb (settings.namOutput, 18.0f)));
        if (! namRunner.isActive())
            return;

        // post EQ at the host rate
        namEq[0].setShelf (false, 110.0, namKnobDb (settings.namBass, 12.0f), fs);
        namEq[1].setPeak (650.0, 0.8, namKnobDb (settings.namMid, 12.0f), fs);
        namEq[2].setShelf (true, 2800.0, namKnobDb (settings.namTreble, 12.0f), fs);
        namEq[3].setShelf (true, 5500.0, namKnobDb (settings.namPresence, 7.0f), fs);
        namEq[4].setPeak (85.0, 0.9, namKnobDb (settings.namDepth, 7.0f), fs);
        for (auto& e : namEq)
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < numSamples; ++i)
                    audio[c][i] = e.process (audio[c][i], c);
    }

    //==============================================================================

    //==============================================================================
    /** RBJ shelf / peak for the NAM post EQ (stereo, double). */
    struct EqBand
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1 {}, z2 {};
        void reset() noexcept { z1 = {}; z2 = {}; }
        void setPeak (double hz, double q, double db, double sr) noexcept
        {
            const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, hz) / sr;
            const double alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
            norm (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
        }
        void setShelf (bool high, double hz, double db, double sr) noexcept
        {
            const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, hz) / sr;
            const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * alpha;
            if (high)
                norm (A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa),
                      (A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa);
            else
                norm (A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa),
                      (A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa);
        }
        void norm (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
        {
            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
        }
        inline float process (float xf, int c) noexcept
        {
            const double x = xf, y = b0 * x + z1[(size_t) c];
            z1[(size_t) c] = b1 * x - a1 * y + z2[(size_t) c];
            z2[(size_t) c] = b2 * x - a2 * y;
            return (float) y;
        }
    };

    //==============================================================================
    Settings settings;
    double fs = 44100.0, fsOs = 352800.0;
    int maxBlock = 512, osLog2 = 3, osFactor = 8;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> dryCopy, namCopy, monoBuffer;

    Coeffs co;
    std::array<float, 8> knobCur {}, lastKnobs {};
    std::array<ampsim::Table, 4> blended;   // allocated with the object (the audio thread only writes into it)
    bool blendReady = false;
    int blendChannel = -1;
    float blendMix = -1.0f;
    bool coeffsDirty = true;
    bool firstUpdate = true;
    int activeChannel = crunch;
    float switchGain = 1.0f, tableMix = 0.0f;
    const std::array<ampsim::Table, 4>* tableA = nullptr;
    const std::array<ampsim::Table, 4>* tableB = nullptr;
    std::array<ChannelState, 1> ch;

    NoiseGate gate;

    juce::SmoothedValue<float> onGain, namMix, level;

    // NAM (amp captures are levelled to a common loudness)
    NamRunner namRunner { true };
    std::array<EqBand, 5> namEq;
};
