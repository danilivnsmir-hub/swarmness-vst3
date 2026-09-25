#pragma once

#include "DSPUtils.h"
#include "PitchVoice.h"
#include <array>

/**
 * SPEED: all-pass feedback network swept by an LFO, plus amplitude modulation at the same rate.
 * Low settings give a slow, seasick phaser; high settings turn into metallic ring-mod shrieks.
 */
class SpeedStage
{
public:
    static constexpr int kStages = 6;

    void prepare (double sr)
    {
        sampleRate = sr;
        mixSmoothed.reset (sr, 0.03);
        mixSmoothed.setCurrentAndTargetValue (0.0f);
        reset();
    }

    void reset()
    {
        for (auto& ch : state)
            ch = {};
        feedback = { 0.0f, 0.0f };
        phase = 0.0;
        counter = 0;
    }

    void setAmount (float s) noexcept
    {
        amount = juce::jlimit (0.0f, 1.0f, s);
        mixSmoothed.setTargetValue (juce::jmin (1.0f, amount * 5.0f));
    }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        if (mixSmoothed.getCurrentValue() <= 0.0f && ! mixSmoothed.isSmoothing())
            return;

        const float rate = 0.5f * std::pow (120.0f, amount);          // 0.5 Hz .. 60 Hz
        const float fb   = 0.25f + 0.62f * amount;
        const float amDepth = 0.35f + 0.65f * amount;
        const double inc = rate / sampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            if ((counter++ & 15) == 0)
            {
                // Exponential sweep 250 Hz .. 3.5 kHz
                const float lfo = 0.5f + 0.5f * std::sin (swarm::kTwoPi * (float) phase);
                const float f = 250.0f * std::pow (14.0f, lfo);
                const float t = std::tan (swarm::kPi * f / (float) sampleRate);
                coeff = (t - 1.0f) / (t + 1.0f);
            }

            const float am  = 1.0f - amDepth * (0.5f + 0.5f * std::sin (swarm::kTwoPi * (float) phase + 1.3f));
            const float mix = mixSmoothed.getNextValue();

            for (int c = 0; c < juce::jmin (numChannels, 2); ++c)
            {
                const float x = audio[c][i];
                float v = x + fb * std::tanh (feedback[(size_t) c]);

                for (int s = 0; s < kStages; ++s)
                {
                    auto& st = state[(size_t) c][(size_t) s];
                    const float y = coeff * v + st;
                    st = v - coeff * y;
                    v = y;
                }

                feedback[(size_t) c] = v;
                const float wet = 0.5f * (x + v) * am * 1.6f;
                audio[c][i] = x + mix * (wet - x);
            }

            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    double sampleRate = 44100.0;
    float amount = 0.0f, coeff = 0.0f;
    std::array<std::array<float, kStages>, 2> state {};
    std::array<float, 2> feedback {};
    double phase = 0.0;
    unsigned int counter = 0;
    juce::SmoothedValue<float> mixSmoothed;
};

//==============================================================================
/**
 * NOISE: a Whammy-style octave shifter played with (momentary) footswitches, inspired by
 * Tallon Electric's "The Noise".
 *
 *  RISE  : time for the pitch to travel to the footswitch interval
 *  FALL  : time to travel back home when the footswitch is released
 *  PANIC : detunes the shifted signal against a second, oppositely detuned voice -> dissonance
 *  CHAOS : random pitch movement away from the centre interval
 *  SPEED : all-pass feedback + amplitude modulation on the shifted signal
 *
 * While no footswitch is held (and the pitch has returned home) the stage passes the input.
 * When engaged the output is MIX shifted (100% = like the pedal).
 *
 * RAW (default): a slight pitch warble and cheap-converter emulation - grainy,
 * lo-fi octaves like the pedal (still in tune and tight on the pick). Off: the clean engine with
 * anti-chipmunk filtering. Both engines use attack restoration.
 */
class NoiseStage
{
public:
    static constexpr int kControlBlock = 32;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        for (auto* v : { &mainVoice, &panicVoice })
        {
            v->prepare (sr, 2);
            v->setRawCharacter (5.0f, 7.0f, 0.0f);   // RAW: slight warble; splices stay tight (footswitch attack)
            v->setLoFi (26000.0f, 13.0f, 10000.0f);  // cheap converters (shared RAW voicing with HIVE)
        }
        speedStage.prepare (sr);
        panicBuffer.setSize (2, juce::jmax (maxBlockSize, kControlBlock), false, false, true);
        dryBuffer  .setSize (2, juce::jmax (maxBlockSize, kControlBlock), false, false, true);
        chaosSmoother.setTime (sr / kControlBlock, 0.012);
        dryLevelCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
        envAttack  = (float) (1.0 - std::exp (-1.0 / (0.0005 * sr)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.025 * sr)));
        gainUp     = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
        gainDown   = (float) (1.0 - std::exp (-1.0 / (0.008 * sr)));
        reset();
    }

    void reset()
    {
        mainVoice.reset();
        panicVoice.reset();
        speedStage.reset();
        currentSemis = targetSemis = 0.0f;
        rampStep = 0.0f;
        wet = 0.0f;
        chaosPhase = 0.0f;
        chaosTarget = 0.0f;
        chaosSmoother.reset (0.0f);
        wobblePhase = 0.0f;
        lastMainRatio = lastPanicRatio = 1.0f;
        restoreGain = 1.0f;
        dryEnv = wetEnv = 0.0f;
        chipmunkLp = { 0.0f, 0.0f };
    }

    /** interval in semitones selected by the footswitches (0 = released). */
    void setInterval (float semis) noexcept
    {
        if (! juce::exactlyEqual (semis, targetSemis))
        {
            // Moving away from home (footswitch down / switching octaves) uses RISE,
            // returning home (footswitch released) uses FALL.
            const bool returning = std::abs (semis) < std::abs (targetSemis) || std::abs (semis) < 0.001f;
            targetSemis = semis;
            const float ms = returning ? fallMs : riseMs;
            const float glideSamples = juce::jmax (1.0f, ms * 0.001f * (float) sampleRate);
            rampStep = std::abs (targetSemis - currentSemis) / glideSamples;
        }
    }

    void setParams (float riseMilliseconds, float fallMilliseconds, float panic01, float chaos01, float speed01,
                    float mix01 = 1.0f, bool rawEngine = true) noexcept
    {
        raw = rawEngine;
        mainVoice.setRaw (raw);
        panicVoice.setRaw (raw);
        mix    = mix01;
        riseMs = riseMilliseconds;
        fallMs = fallMilliseconds;
        panic  = panic01;
        chaos  = chaos01;
        speed  = speed01;
    }

    /** Level of the unshifted (dry) part of the output - HIVE MIX turns it down to leave only the effects. */
    void setDryLevel (float level01) noexcept { dryLevelTarget = level01; }

    bool isEngaged() const noexcept { return std::abs (targetSemis) > 0.001f || std::abs (currentSemis) > 0.001f; }

    /** Current transposition including Chaos / Panic movement (for the display). */
    float getCurrentSemitones() const noexcept { return displaySemis; }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, 2);

        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, audio[ch], numSamples);

        speedStage.setAmount (speed);

        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = juce::jmin (kControlBlock, numSamples - start);

            // Rise: linear glide in semitones (Whammy-style)
            const float stepThisBlock = rampStep * (float) n;
            if (currentSemis < targetSemis) currentSemis = juce::jmin (targetSemis, currentSemis + stepThisBlock);
            else                            currentSemis = juce::jmax (targetSemis, currentSemis - stepThisBlock);

            const bool active = isEngaged();
            const float engage = active ? 1.0f : 0.0f;

            // Chaos: random targets away from the centre, faster and wider as it goes up
            const float dt = (float) n / (float) sampleRate;
            chaosPhase += (2.0f + 16.0f * chaos) * dt;
            if (chaosPhase >= 1.0f)
            {
                chaosPhase -= std::floor (chaosPhase);
                const float range = std::pow (chaos, 1.5f) * 12.0f;
                chaosTarget = rng.nextBipolar() * range;
            }
            if (chaos <= 0.0001f) chaosTarget = 0.0f;
            const float chaosOffset = chaosSmoother.advance (chaosTarget * engage, 1);

            // Panic: split the signal into two voices detuned against each other
            wobblePhase += 0.37f * dt;
            if (wobblePhase >= 1.0f) wobblePhase -= 1.0f;
            const float wobble = panic > 0.5f ? (panic - 0.5f) * 0.6f * std::sin (swarm::kTwoPi * wobblePhase) : 0.0f;
            const float detune = (std::pow (panic, 1.3f) * 1.5f + wobble) * engage;

            const float mainSemis  = currentSemis + chaosOffset + 0.5f * detune;
            const float panicSemis = currentSemis + chaosOffset - 0.5f * detune;
            displaySemis = active ? mainSemis : 0.0f;

            const float mainRatio  = std::pow (2.0f, mainSemis / 12.0f);
            const float panicRatio = std::pow (2.0f, panicSemis / 12.0f);

            float* sub[2] = { audio[0] + start, audio[numChannels > 1 ? 1 : 0] + start };
            const bool usePanic = panic > 0.001f || panicLevel > 0.001f;

            if (usePanic)
                for (int ch = 0; ch < numChannels; ++ch)
                    panicBuffer.copyFrom (ch, 0, sub[ch], n);

            mainVoice.process (sub, numChannels, n, lastMainRatio, mainRatio);
            lastMainRatio = mainRatio;

            if (usePanic)
            {
                float* psub[2] = { panicBuffer.getWritePointer (0), panicBuffer.getWritePointer (numChannels > 1 ? 1 : 0) };
                panicVoice.process (psub, numChannels, n, lastPanicRatio, panicRatio);
                lastPanicRatio = panicRatio;

                const float targetLevel = juce::jmin (1.0f, panic * 2.5f) * 0.85f;
                for (int i = 0; i < n; ++i)
                {
                    panicLevel += 0.002f * (targetLevel - panicLevel);
                    const float norm = 1.0f / (1.0f + 0.45f * panicLevel);
                    for (int ch = 0; ch < numChannels; ++ch)
                        sub[ch][i] = (sub[ch][i] + panicLevel * psub[ch][i]) * norm;
                }
            }

            if (active)
            {
                // Anti-chipmunk: darken up-shifts in proportion to the shift
                // (modern engine only - in RAW the converter emulation does the darkening)
                const float ratio = std::pow (2.0f, juce::jmax (0.0f, mainSemis) / 12.0f);
                const float lpHz = 16000.0f / std::pow (ratio, 0.9f);
                const float lpCoeff = mainVoice.isRaw() ? 0.0f : std::exp (-swarm::kTwoPi * lpHz / (float) sampleRate);

                for (int i = 0; i < n; ++i)
                {
                    // Attack restoration: the splicing shifter smears transients, so the shifted
                    // signal follows the dry signal's envelope (bounded correction, fast attack).
                    float dryA = 0.0f, wetA = 0.0f;
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        dryA = juce::jmax (dryA, std::abs (dryBuffer.getSample (ch, start + i)));
                        wetA = juce::jmax (wetA, std::abs (sub[ch][i]));
                    }
                    dryEnv += (dryA > dryEnv ? envAttack : envRelease) * (dryA - dryEnv);
                    wetEnv += (wetA > wetEnv ? envAttack : envRelease) * (wetA - wetEnv);
                    const float target = juce::jlimit (0.6f, 2.0f, (dryEnv + 1.0e-4f) / (wetEnv + 1.0e-4f));
                    restoreGain += (target > restoreGain ? gainUp : gainDown) * (target - restoreGain);

                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        auto& z = chipmunkLp[(size_t) ch];
                        z = sub[ch][i] + lpCoeff * (z - sub[ch][i]);
                        sub[ch][i] = z * restoreGain;
                    }
                }
            }
            else
            {
                restoreGain = 1.0f;
                dryEnv = wetEnv = 0.0f;
                chipmunkLp = { 0.0f, 0.0f };
            }

            // Speed only colours the shifted signal.
            if (active || wet > 0.0001f)
                speedStage.process (sub, numChannels, n);

            // Wet envelope: MIX amount of shifted signal while engaged, dry once released and home.
            // Equal-power law: the shifted voice is not phase-coherent with the dry signal.
            for (int i = 0; i < n; ++i)
            {
                wet += 0.004f * (engage * mix - wet);
                dryLevel += dryLevelCoeff * (dryLevelTarget - dryLevel);
                if (std::abs (dryLevel - dryLevelTarget) < 1.0e-6f)
                    dryLevel = dryLevelTarget;

                if (wet < 0.9999f)
                {
                    float dryG, wetG;
                    swarm::equalPowerGains (wet, dryG, wetG);
                    dryG *= dryLevel;
                    for (int ch = 0; ch < numChannels; ++ch)
                        sub[ch][i] = dryG * dryBuffer.getSample (ch, start + i) + wetG * sub[ch][i];
                }
            }
        }
    }

private:
    double sampleRate = 44100.0;
    PitchVoice mainVoice, panicVoice;
    bool raw = true;
    SpeedStage speedStage;
    juce::AudioBuffer<float> panicBuffer, dryBuffer;
    swarm::FastRandom rng { 0xBADC0DEu };
    swarm::OnePole chaosSmoother;

    float riseMs = 30.0f, fallMs = 30.0f, mix = 1.0f, panic = 0.0f, chaos = 0.0f, speed = 0.0f;
    float currentSemis = 0.0f, targetSemis = 0.0f, rampStep = 0.0f, displaySemis = 0.0f;
    float wet = 0.0f, panicLevel = 0.0f;
    float chaosPhase = 0.0f, chaosTarget = 0.0f, wobblePhase = 0.0f;
    float lastMainRatio = 1.0f, lastPanicRatio = 1.0f;

    // attack restoration / anti-chipmunk
    float dryEnv = 0.0f, wetEnv = 0.0f, restoreGain = 1.0f;
    float dryLevel = 1.0f, dryLevelTarget = 1.0f, dryLevelCoeff = 0.001f;
    float envAttack = 0.05f, envRelease = 0.001f, gainUp = 0.02f, gainDown = 0.003f;
    std::array<float, 2> chipmunkLp {};
};
