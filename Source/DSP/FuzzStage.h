#pragma once

#include "DSPUtils.h"
#include <array>

/**
 * SMOKE: high-gain, Muff-family fuzz voiced after the Swollen Pickle and the Cathedral,
 * oversampled to ~176-192 kHz (linear-phase FIR half-band filters).
 *
 *  VOICE : pre-clip EQ. DOWN = doom low-mids with the full bottom end, MID = classic
 *          jumbo fuzz, UP = tight low end and screaming upper mids
 *  FUZZ  : from dirty crunch to wall-of-fuzz sustain (three cascaded clipping stages
 *          with inter-stage low-passes, like the transistor/diode stages of a Muff)
 *  TONE  : tilt around 800 Hz plus a fizz low-pass that opens with the tone
 *  SCOOP : mid cut depth (0 = flat mids, max = deep scoop); the centre follows VOICE
 *  GLARE : rectified octave-up mixed between the gain stages, gated so it only rips
 *          through on hard picking
 *  GATE  : starves the fuzz - bias shift plus an envelope gate that makes decays sputter
 *  BLEND : clean (latency-aligned) signal added under the fuzz for pick attack and low end
 *  SAG   : how much the circuit "breathes": supply sag compresses and darkens the attack, then
 *          the note blooms back as the supply recovers (longer recovery at higher settings)
 *
 * Living-circuit behaviour (always on, scaled by SAG): the coupling capacitor between the
 * stages charges with the note, so its asymmetry changes as it rings out; the tone is brighter
 * on the pick and darker in the decay; and the parts drift slowly and independently per channel.
 *
 * Built-in noise gate (always on, like the gate every high-gain rig needs): a soft
 * downward expander on the fuzz input. Around 50 dB of fuzz gain would otherwise turn
 * interface hiss and pickup hum into a roar between notes. It opens instantly on a pick,
 * holds briefly and fades smoothly, so note decays are not chopped.
 *
 * Oversampling adapts to the host rate (4x / 2x / none, always ~176-192 kHz inside). When off,
 * the stage outputs a latency-matched clean path and skips all processing, so the reported
 * latency never changes and "off" is bit-transparent.
 */
class FuzzStage
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlBlock = 32;
    static constexpr float kGateThresholdDb = -58.0f;   // input peak level (after INPUT) where the gate is fully open

    struct Settings
    {
        float fuzz = 0.7f, tone = 0.5f, scoop = 0.4f, glare = 0.0f, gate = 0.0f, blend = 0.0f, sag = 0.4f;
        int voice = 1;   // 0 = down, 1 = mid, 2 = up
    };

    FuzzStage()
    {
        for (auto& f : preHp)     f.setType (swarm::SVF::Type::highPass);
        for (auto& f : preBoost)  f.setType (swarm::SVF::Type::bell);
        for (auto& f : scoopBell) f.setType (swarm::SVF::Type::bell);
        for (auto& f : thump)     f.setType (swarm::SVF::Type::bell);
        for (auto& f : fizzLp)    f.setType (swarm::SVF::Type::lowPass);
    }

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        // Oversample to ~176-192 kHz: 4x at 44.1/48 kHz, 2x at 88.2/96 kHz, none at 176.4/192 kHz
        osFactorLog2 = sr < 60000.0 ? 2 : (sr < 120000.0 ? 1 : 0);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            kMaxChannels, (size_t) osFactorLog2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlockSize);
        oversampler->reset();

        cleanDelay.setMaximumDelayInSamples (getLatencySamples() + 4);
        cleanDelay.prepare ({ sr, (juce::uint32) maxBlockSize, (juce::uint32) kMaxChannels });
        cleanDelay.setDelay ((float) getLatencySamples());
        cleanCopy.setSize (kMaxChannels, maxBlockSize, false, false, true);

        onMix.reset (sr, 0.02);
        onMix.setCurrentAndTargetValue (isOn ? 1.0f : 0.0f);

        const double osRate = sr * (double) (1 << osFactorLog2);
        osSampleRate = osRate;
        gateEnvRelease  = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        gateOpen        = (float) (1.0 - std::exp (-1.0 / (0.0005 * sr)));
        gateClose       = (float) (1.0 - std::exp (-1.0 / (0.08 * sr)));
        gateHoldSamples = (int) (0.04 * sr);
        sagAttack  = (float) (1.0 - std::exp (-1.0 / (0.002 * osRate)));
        slowAttack  = (float) (1.0 - std::exp (-1.0 / (0.04 * osRate)));
        slowRelease = (float) (1.0 - std::exp (-1.0 / (0.5 * osRate)));
        capCoeff   = (float) (1.0 - std::exp (-1.0 / (0.025 * osRate)));
        for (auto& d : driftGain) d.setTime (sr / kControlBlock, 0.8);
        envAttack  = (float) (1.0 - std::exp (-1.0 / (0.0007 * osRate)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.04 * osRate)));
        lp1Coeff   = (float) std::exp (-swarm::kTwoPi * 5500.0 / osRate);
        lp2Coeff   = (float) std::exp (-swarm::kTwoPi * 8000.0 / osRate);
        octHpCoeff = (float) std::exp (-swarm::kTwoPi * 90.0 / osRate);
        tiltCoeff  = (float) std::exp (-swarm::kTwoPi * 800.0 / sr);

        // Control smoothing, advanced once per control block
        const double controlRate = sr / kControlBlock;
        for (auto* s : { &sFuzz, &sGate, &sGlare, &sTone, &sScoop, &sBlend, &sSag,
                         &sHpFreq, &sBoostFreq, &sBoostDb, &sScoopFreq, &sThumpDb, &sTrimDb })
            s->setTime (controlRate, 0.03);
        snapControls();

        for (auto& d : dc) d.prepare (sr);
        for (auto& d : preDc) d.prepare (osRate);
        reset();
    }

    void reset()
    {
        if (oversampler != nullptr) oversampler->reset();
        cleanDelay.reset();
        for (auto& d : dc) d.reset();
        for (auto& d : preDc) d.reset();
        for (auto* bank : { &preHp, &preBoost, &scoopBell, &thump, &fizzLp })
            for (auto& f : *bank) f.reset();
        env = lp1 = lp2 = octHpState = octHpIn = tiltLp = { 0.0f, 0.0f };
        gateEnv = 0.0f;
        gateGain = 0.0f;
        sagEnv = slowEnv = capState = { 0.0f, 0.0f };
        for (auto& d : driftGain) d.reset (1.0f);
        gateHoldCounter = 0;
    }

    int getLatencySamples() const { return oversampler != nullptr ? (int) std::round (oversampler->getLatencyInSamples()) : 0; }

    void setParams (bool on, const Settings& s) noexcept
    {
        isOn = on;
        target = s;
        onMix.setTargetValue (on ? 1.0f : 0.0f);
    }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, kMaxChannels);
        juce::dsp::AudioBlock<float> sub (audio, (size_t) numChannels, (size_t) numSamples);

        for (int c = 0; c < numChannels; ++c)
        {
            float* dst = cleanCopy.getWritePointer (c);
            for (int i = 0; i < numSamples; ++i)
            {
                cleanDelay.pushSample (c, audio[c][i]);
                dst[i] = cleanDelay.popSample (c);
            }
        }

        const bool active = isOn || onMix.isSmoothing() || onMix.getCurrentValue() > 0.0f;
        if (! active)
        {
            // Off: just the latency-aligned clean signal (no oversampling, no CPU).
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::copy (audio[c], cleanCopy.getReadPointer (c), numSamples);
            wasIdle = true;
            return;
        }
        if (wasIdle)
        {
            // Waking up: start the oversampler from silence (the on/off crossfade hides the start).
            oversampler->reset();
            wasIdle = false;
        }

        // ---- noise gate + pre-clip EQ (base rate), per control block so VOICE changes glide
        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = juce::jmin (kControlBlock, numSamples - start);
            advanceControls();

            for (int i = start; i < start + n; ++i)
            {
                float a = 0.0f;
                for (int c = 0; c < numChannels; ++c)
                    a = juce::jmax (a, std::abs (audio[c][i]));
                gateEnv = a > gateEnv ? a : gateEnv + gateEnvRelease * (a - gateEnv);

                // Expander: fully open above the threshold, closed 8 dB below it (squared, in dB)
                const float db = juce::Decibels::gainToDecibels (gateEnv, -120.0f);
                float target = juce::jlimit (0.0f, 1.0f, (db - (kGateThresholdDb - 8.0f)) / 8.0f);
                target *= target;
                if (target >= 1.0f)
                    gateHoldCounter = gateHoldSamples;
                else if (gateHoldCounter > 0)
                {
                    --gateHoldCounter;
                    target = 1.0f;
                }
                gateGain += (target > gateGain ? gateOpen : gateClose) * (target - gateGain);

                for (int c = 0; c < numChannels; ++c)
                    audio[c][i] *= gateGain;
            }

            for (int c = 0; c < numChannels; ++c)
            {
                preHp[(size_t) c].setParams (sampleRate, sHpFreq.get(), 0.7071f);
                preBoost[(size_t) c].setParams (sampleRate, sBoostFreq.get(), 0.75f, sBoostDb.get());
                float* d = audio[c] + start;
                for (int i = 0; i < n; ++i)
                    d[i] = preBoost[(size_t) c].process (preHp[(size_t) c].process (d[i]));
            }
        }

        // ---- clipping stages (4x oversampled)
        auto up = oversampler->processSamplesUp (sub);
        {
            const int upSamples = (int) up.getNumSamples();
            const float f = sFuzz.get(), g = sGate.get(), gl = sGlare.get();

            const float gain1 = juce::Decibels::decibelsToGain (4.0f + 30.0f * f);
            const float gain2 = juce::Decibels::decibelsToGain (6.0f + 22.0f * f);
            const float drive3 = 1.0f + 2.5f * f;                  // "Armageddon" squaring at high FUZZ
            const float norm3 = 1.0f / std::tanh (drive3);
            const float bias = 0.45f * g;
            const float threshold = 0.06f * g * g;
            const float glareAmt = 2.2f * gl;
            const float sag = sSag.get();
            // supply recovery: longer at high SAG -> slower bloom
            const float sagRelease = (float) (1.0 - std::exp (-1.0 / ((0.04 + 0.16 * sag) * osSampleRate)));
            const float capDepth = 0.35f + 0.65f * sag;   // coupling-cap memory, always a little

            // slow thermal drift of the parts, independent per channel
            driftCounter += numSamples;
            if (driftCounter >= (int) (0.6 * sampleRate))
            {
                driftCounter = 0;
                for (int c = 0; c < kMaxChannels; ++c)
                {
                    driftGainTarget[(size_t) c] = 1.0f + 0.05f * rng.nextBipolar();
                }
            }

            for (int c = 0; c < numChannels; ++c)
            {
                float* data = up.getChannelPointer ((size_t) c);
                const float dGain = driftGain[(size_t) c].advance (driftGainTarget[(size_t) c], numSamples / kControlBlock + 1);
                auto& se = sagEnv[(size_t) c];
                auto& slow = slowEnv[(size_t) c];
                auto& cap = capState[(size_t) c];
                auto& e = env[(size_t) c];
                auto& l1 = lp1[(size_t) c];
                auto& l2 = lp2[(size_t) c];
                auto& ohs = octHpState[(size_t) c];
                auto& ohi = octHpIn[(size_t) c];

                for (int i = 0; i < upSamples; ++i)
                {
                    const float x = data[i];

                    const float a = std::abs (x);
                    e += (a > e ? envAttack : envRelease) * (a - e);

                    // Supply sag: the pick's current spike (fast vs. slow envelope) pulls the supply
                    // down; it recovers while the note sustains -> the note blooms back.
                    slow += (a > slow ? slowAttack : slowRelease) * (a - slow);
                    const float spike = juce::jlimit (0.0f, 1.0f, (e - slow) / (e + 1.0e-4f) * 1.6f);
                    se += (spike > se ? sagAttack : sagRelease) * (spike - se);
                    const float droop = sag * se;

                    // Stage 1: asymmetric transistor-ish clipper (+ starve bias), smoothed
                    const float v1 = x * gain1 * dGain * (1.0f - 0.55f * droop) + bias + 0.15f * droop;
                    float y = v1 >= 0.0f ? std::tanh (v1) : 1.25f * std::tanh (0.7f * v1);
                    l1 = y + lp1Coeff * (l1 - y);
                    y = l1;

                    // Coupling capacitor: charges with the note's DC, so the next stage's
                    // asymmetry (and its "blat") changes as the note rings out
                    cap += capCoeff * (y - cap);
                    y -= capDepth * cap;

                    // GLARE: full-wave rectified octave, DC-free, only on hard notes
                    if (glareAmt > 0.0f)
                    {
                        const float r = std::abs (std::tanh (x * 6.0f));
                        ohs = octHpCoeff * (ohs + r - ohi);
                        ohi = r;
                        const float open = juce::jlimit (0.0f, 1.0f, (e - 0.04f) * 12.0f);
                        y += glareAmt * open * ohs;
                    }

                    // Stage 2: diode pair (cubic soft clip, hard-ish knee), smoothed
                    float v2 = juce::jlimit (-1.5f, 1.5f, y * gain2 * (1.0f - 0.3f * droop));
                    v2 = v2 - (4.0f / 27.0f) * v2 * v2 * v2;
                    l2 = v2 + lp2Coeff * (l2 - v2);

                    // Stage 3: squaring; the sagging supply also pulls the output down a little
                    float out = norm3 * std::tanh (drive3 * l2) * (1.0f - 0.4f * droop);

                    // Starve gate: sputters as the note decays
                    if (threshold > 0.0f)
                    {
                        const float open = juce::jlimit (0.0f, 1.0f, (e - 0.5f * threshold) / (0.5f * threshold));
                        out *= open * open;
                    }

                    data[i] = preDc[(size_t) c].process (out);
                }
            }
        }
        oversampler->processSamplesDown (sub);

        // ---- tone stack, scoop, output (base rate)
        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = juce::jmin (kControlBlock, numSamples - start);
            const float t = 2.0f * sTone.get() - 1.0f;                        // -1 dark .. +1 bright
            const float lowG  = juce::Decibels::decibelsToGain (-5.0f * t);
            const float highG = juce::Decibels::decibelsToGain (9.0f * t - 2.0f);
            // brighter on the pick, darker in the decay (more so with SAG)
            const float pick = juce::jlimit (0.0f, 1.0f, gateEnv * 5.0f);
            const float dyn = 0.35f + 0.5f * sSag.get();
            const float fizzHz = 3200.0f * std::pow (3.5f, sTone.get()) * (1.0f - dyn * 0.45f + dyn * 0.55f * pick);
            const float scoopDb = -20.0f * sScoop.get();
            const float blend = sBlend.get();
            // Voice trim, plus make-up at low FUZZ where the clippers do not compress yet
            const float lowFuzz = 1.0f - sFuzz.get();
            const float level = 0.42f * juce::Decibels::decibelsToGain (sTrimDb.get() + 6.0f * lowFuzz * lowFuzz);

            for (int c = 0; c < numChannels; ++c)
            {
                scoopBell[(size_t) c].setParams (sampleRate, sScoopFreq.get(), 0.7f, scoopDb);
                thump[(size_t) c].setParams (sampleRate, 110.0f, 0.8f, sThumpDb.get());
                fizzLp[(size_t) c].setParams (sampleRate, fizzHz, 0.7071f);
            }

            for (int i = start; i < start + n; ++i)
            {
                const float mixNow = onMix.getNextValue();
                for (int c = 0; c < numChannels; ++c)
                {
                    const float clean = cleanCopy.getSample (c, i);
                    float x = dc[(size_t) c].process (audio[c][i]);

                    // Tilt: complementary one-pole split around 800 Hz
                    auto& tl = tiltLp[(size_t) c];
                    tl = x + tiltCoeff * (tl - x);
                    x = lowG * tl + highG * (x - tl);

                    x = scoopBell[(size_t) c].process (x);
                    x = thump[(size_t) c].process (x);
                    x = fizzLp[(size_t) c].process (x);

                    const float fx = level * x + blend * clean;
                    audio[c][i] = clean + mixNow * (fx - clean);
                }
            }
        }
    }

private:
    struct VoiceShape { float hpHz, boostHz, boostDb, scoopHz, thumpDb, trimDb; };

    static VoiceShape voiceShape (int voice) noexcept
    {
        switch (voice)
        {
            case 0:  return { 30.0f,  200.0f, 7.0f, 900.0f, 5.0f, -3.5f };   // DOWN: doom, low-mid growl
            case 2:  return { 150.0f, 1500.0f, 9.0f, 450.0f, 0.0f, 2.5f };  // UP: tight, screaming upper mids
            default: return { 60.0f,  650.0f, 3.0f, 700.0f, 3.0f, -1.5f };   // MID: jumbo fuzz
        }
    }

    void setControlTargets (bool snap) noexcept
    {
        const auto v = voiceShape (target.voice);
        auto set = [snap] (swarm::OnePole& s, float value) { if (snap) s.reset (value); else s.process (value); };
        set (sFuzz, target.fuzz);   set (sGate, target.gate);     set (sGlare, target.glare);
        set (sTone, target.tone);   set (sScoop, target.scoop);   set (sBlend, target.blend);
        set (sSag, target.sag);
        set (sHpFreq, v.hpHz);      set (sBoostFreq, v.boostHz);  set (sBoostDb, v.boostDb);
        set (sScoopFreq, v.scoopHz); set (sThumpDb, v.thumpDb); set (sTrimDb, v.trimDb);
    }

    void snapControls() noexcept    { setControlTargets (true); }
    void advanceControls() noexcept { setControlTargets (false); }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int osFactorLog2 = 2;
    bool wasIdle = true;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> cleanDelay { 1 };
    juce::AudioBuffer<float> cleanCopy;
    juce::SmoothedValue<float> onMix;

    swarm::OnePole sFuzz, sGate, sGlare, sTone, sScoop, sBlend, sSag, sHpFreq, sBoostFreq, sBoostDb, sScoopFreq, sThumpDb, sTrimDb;

    std::array<swarm::DCBlocker, kMaxChannels> dc, preDc;
    std::array<swarm::SVF, kMaxChannels> preHp, preBoost, scoopBell, thump, fizzLp;
    std::array<float, kMaxChannels> env {}, lp1 {}, lp2 {}, octHpState {}, octHpIn {}, tiltLp {};

    double sampleRate = 44100.0, osSampleRate = 176400.0;
    bool isOn = false;
    Settings target;
    std::array<float, kMaxChannels> sagEnv {}, slowEnv {}, capState {}, driftGainTarget { 1.0f, 1.0f };
    std::array<swarm::OnePole, kMaxChannels> driftGain;
    swarm::FastRandom rng { 0xF022F022u };
    int driftCounter = 0;
    float sagAttack = 0.001f, capCoeff = 0.0001f, slowAttack = 0.0001f, slowRelease = 0.00001f;
    float gateEnv = 0.0f, gateGain = 0.0f, gateEnvRelease = 0.001f, gateOpen = 0.04f, gateClose = 0.0003f;
    int gateHoldSamples = 2000, gateHoldCounter = 0;
    float envAttack = 0.1f, envRelease = 0.001f, lp1Coeff = 0.9f, lp2Coeff = 0.9f, octHpCoeff = 0.99f, tiltCoeff = 0.9f;
};
