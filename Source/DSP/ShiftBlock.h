#pragma once

#include "DSPUtils.h"
#include "PitchVoice.h"
#include "SpeedStage.h"
#include <array>

/**
 * SHIFT - the footswitch pitch shifter (Whammy / Tallon "The Noise" style). It has no power
 * button: like a momentary pedal it only sounds while a footswitch is held.
 *
 *  SHIFT A / SHIFT B  footswitch intervals (-24..+24 st). Holding both: B wins - or with STACK
 *                     both sound at once (A stays, a second voice splits off to B; releasing B
 *                     merges it back into A)
 *  RISE / FALL        glide into the interval / back home
 *  BLEND              how much the shifted note replaces the dry one (100% = only shifted)
 *  ANGER              a second voice detuned against the shifted one (sour beating, "Panic")
 *  FRENZY             random pitch jumps ("Chaos"); SNAP = jumps land on 4ths / 5ths / octaves
 *  BUZZ               all-pass feedback + AM ("Speed")
 *  DETUNE, RAW        fine offset in cents; cheap-pedal-DSP character
 */
class ShiftBlock
{
public:
    static constexpr int kControlBlock = 32;

    struct Settings
    {
        bool shiftA = false, shiftB = false, stack = false;
        float shiftASemis = 12.0f, shiftBSemis = 24.0f;
        float riseMs = 30.0f, fallMs = 30.0f, blend = 1.0f;
        float anger = 0.0f, frenzy = 0.0f, buzz = 0.0f, detuneCents = 0.0f;
        bool raw = true, snap = true;
    };

    void prepare (double sr, int maxBlockSize)
    {
        (void) maxBlockSize;
        sampleRate = sr;
        ring.prepare (sr, 2);
        for (auto* v : { &mainVoice, &angerVoice, &stackVoice })
        {
            v->prepare (ring);
            v->setRawCharacter (5.0f, 7.0f, 0.0f);   // RAW: slight warble; splices stay tight (footswitch attack)
            v->setLoFi (26000.0f, 13.0f, 10000.0f);
        }
        for (auto* b : { &dry, &shifted, &angerBuf, &stackBuf })
            b->setSize (2, kControlBlock, false, false, true);
        speed.prepare (sr);
        chaosSmoother.setTime (sr / kControlBlock, 0.012);
        envAttack  = (float) (1.0 - std::exp (-1.0 / (0.0005 * sr)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.025 * sr)));
        gainUp     = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
        gainDown   = (float) (1.0 - std::exp (-1.0 / (0.008 * sr)));
        for (auto& f : subsonic)
        {
            f.setType (swarm::SVF::Type::highPass);
            f.setParams (sr, 38.0f, 0.7071f);
        }
        reset();
    }

    void reset()
    {
        ring.clear();
        for (auto* v : { &mainVoice, &angerVoice, &stackVoice })
            v->reset();
        speed.reset();
        currentSemis = targetSemis = rampStep = 0.0f;
        stackSemis = stackTarget = stackStep = stackLevel = 0.0f;
        stackActive = false;
        wet = angerLevel = 0.0f;
        chaosPhase = chaosTarget = wobblePhase = 0.0f;
        chaosSmoother.reset (0.0f);
        restoreGain = 1.0f;
        dryEnv = wetEnv = 0.0f;
        chipmunkLp = { 0.0f, 0.0f };
        for (auto& f : subsonic) f.reset();
        idle = stackIdle = true;
        lastMain = lastAnger = lastStack = 1.0f;
        displaySemis = 0.0f;
    }

    void setParams (const Settings& s) noexcept
    {
        settings = s;
        for (auto* v : { &mainVoice, &angerVoice, &stackVoice })
            v->setRaw (s.raw);
        speed.setAmount (s.buzz);

        // Main voice: B wins over A (like +2 OCT over +1 OCT) - unless STACK keeps A while B is added
        const bool stacked = s.stack && s.shiftA && s.shiftB;
        const float semis = stacked ? s.shiftASemis : (s.shiftB ? s.shiftBSemis : (s.shiftA ? s.shiftASemis : 0.0f));
        if (! sw::exactlyEqual (semis, targetSemis))
        {
            // Moving away from home uses RISE, returning home (released) uses FALL.
            const bool returning = std::abs (semis) < std::abs (targetSemis) || std::abs (semis) < 0.001f;
            targetSemis = semis;
            rampStep = std::abs (targetSemis - currentSemis) / samplesFor (returning ? s.fallMs : s.riseMs);
        }

        // STACK voice: splits off the A voice towards B (RISE), merges back into it on release (FALL)
        if (stacked != stackActive)
        {
            stackActive = stacked;
            if (stacked && stackLevel <= 0.0f)
                stackSemis = currentSemis;
            stackTarget = stacked ? s.shiftBSemis : targetSemis;
            stackStep = std::abs (stackTarget - stackSemis) / samplesFor (stacked ? s.riseMs : s.fallMs);
        }
        else if (stacked && ! sw::exactlyEqual (stackTarget, s.shiftBSemis))
        {
            stackTarget = s.shiftBSemis;
            stackStep = std::abs (stackTarget - stackSemis) / samplesFor (s.riseMs);
        }
    }

    bool isEngaged() const noexcept      { return std::abs (targetSemis) > 0.001f || std::abs (currentSemis) > 0.001f || stackLevel > 0.0f; }
    float getSemitones() const noexcept  { return displaySemis; }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = sw::jmin (numChannels, 2);
        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = sw::jmin (kControlBlock, numSamples - start);
            float* sub[2] = { audio[0] + start, audio[numChannels > 1 ? 1 : 0] + start };
            processControlBlock (sub, numChannels, n);
        }
    }

private:
    float samplesFor (float ms) const noexcept { return sw::jmax (1.0f, ms * 0.001f * (float) sampleRate); }
    static float ratioFor (float semis) noexcept { return std::pow (2.0f, semis / 12.0f); }

    static void glide (float& current, float target, float step) noexcept
    {
        if (current < target) current = sw::jmin (target, current + step);
        else                  current = sw::jmax (target, current - step);
    }

    void processControlBlock (float* const* audio, int numChannels, int n) noexcept
    {
        const auto& s = settings;
        const float dt = (float) n / (float) sampleRate;

        glide (currentSemis, targetSemis, rampStep * (float) n);
        if (! stackActive)
            stackTarget = currentSemis;   // released: follow the A voice home while fading out
        glide (stackSemis, stackTarget, stackStep * (float) n);

        const bool engaged = isEngaged();
        const float engage = engaged ? 1.0f : 0.0f;

        // FRENZY: random pitch targets (SNAP = musical intervals); ANGER: detune (+ slow wobble when high)
        chaosPhase += (2.0f + 16.0f * s.frenzy) * dt;
        if (chaosPhase >= 1.0f)
        {
            chaosPhase -= std::floor (chaosPhase);
            const float range = std::pow (s.frenzy, 1.5f) * 12.0f;
            if (s.snap)
            {
                static constexpr float steps[] { -12.0f, -7.0f, -5.0f, 0.0f, 5.0f, 7.0f, 12.0f };
                float pick = 0.0f;
                for (int tries = 0; tries < 4; ++tries)
                {
                    pick = steps[(size_t) (rng.nextInt() % 7u)];
                    if (std::abs (pick) <= range + 0.5f)
                        break;
                    pick = 0.0f;
                }
                chaosTarget = pick;
            }
            else
            {
                chaosTarget = rng.nextBipolar() * range;
            }
        }
        if (s.frenzy <= 0.0001f) chaosTarget = 0.0f;
        const float chaos = chaosSmoother.advance (chaosTarget, 1) * engage;

        wobblePhase += 0.37f * dt;
        if (wobblePhase >= 1.0f) wobblePhase -= 1.0f;
        const float wobble = s.anger > 0.5f ? (s.anger - 0.5f) * 0.6f * std::sin (swarm::kTwoPi * wobblePhase) : 0.0f;
        const float anger = (std::pow (s.anger, 1.3f) * 1.5f + wobble) * engage;
        const float fine = s.detuneCents * 0.01f * engage;

        for (int ch = 0; ch < numChannels; ++ch)
            dry.copyFrom (ch, 0, audio[ch], n);
        const int base = ring.write (audio, numChannels, n);

        const bool running = engaged || wet > 1.0e-5f;
        if (running)
            processVoices (numChannels, n, base, engage, chaos, anger, fine);
        else
            idle = true;
        displaySemis = engaged ? currentSemis + chaos + 0.5f * anger + fine : 0.0f;

        // dry / shifted (BLEND while engaged)
        for (int i = 0; i < n; ++i)
        {
            wet += 0.004f * (engage * s.blend - wet);
            if (! engaged && wet < 1.0e-5f)
                wet = 0.0f;
            float dryG, wetG;
            swarm::equalPowerGains (wet, dryG, wetG);
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] = dryG * dry.getSample (ch, i) + (running ? wetG * shifted.getSample (ch, i) : 0.0f);
        }
    }

    void processVoices (int numChannels, int n, int base, float engage, float chaos, float anger, float fine) noexcept
    {
        const auto& s = settings;
        if (idle)
        {
            mainVoice.reset();
            angerVoice.reset();
            speed.reset();
            lastMain = lastAnger = 1.0f;
            idle = false;
        }

        const float mainSemis  = currentSemis + chaos + 0.5f * anger + fine;
        const float angerSemis = currentSemis + chaos - 0.5f * anger + fine;
        const float mainRatio = ratioFor (mainSemis), angerRatio = ratioFor (angerSemis);

        float* out[2] = { shifted.getWritePointer (0), shifted.getWritePointer (1) };
        mainVoice.process (out, numChannels, n, base, lastMain, mainRatio);
        lastMain = mainRatio;

        // ANGER: a second voice detuned the other way
        if (s.anger > 0.001f || angerLevel > 0.001f)
        {
            float* a[2] = { angerBuf.getWritePointer (0), angerBuf.getWritePointer (1) };
            angerVoice.process (a, numChannels, n, base, lastAnger, angerRatio);
            lastAnger = angerRatio;
            const float targetLevel = sw::jmin (1.0f, s.anger * 2.5f) * 0.85f;
            for (int i = 0; i < n; ++i)
            {
                angerLevel += 0.002f * (targetLevel - angerLevel);
                const float norm = 1.0f / (1.0f + 0.45f * angerLevel);
                for (int ch = 0; ch < numChannels; ++ch)
                    out[ch][i] = (out[ch][i] + angerLevel * a[ch][i]) * norm;
            }
        }

        // STACK: the B voice on top of the A voice
        const float stackGoal = stackActive ? 1.0f : 0.0f;
        if (stackActive || stackLevel > 0.0f)
        {
            if (stackIdle)
            {
                stackVoice.reset();
                lastStack = ratioFor (stackSemis);
                stackIdle = false;
            }
            const float stackRatio = ratioFor (stackSemis + chaos - 0.5f * anger - fine);
            float* b[2] = { stackBuf.getWritePointer (0), stackBuf.getWritePointer (1) };
            stackVoice.process (b, numChannels, n, base, lastStack, stackRatio);
            lastStack = stackRatio;
            const float coeff = 1.0f / samplesFor (stackActive ? 12.0f : sw::jmax (20.0f, s.fallMs));
            for (int i = 0; i < n; ++i)
            {
                stackLevel += coeff * (stackGoal - stackLevel) * 3.0f;
                if (! stackActive && stackLevel < 1.0e-4f)
                    stackLevel = 0.0f;
                const float norm = 1.0f / (1.0f + 0.41f * stackLevel);
                for (int ch = 0; ch < numChannels; ++ch)
                    out[ch][i] = (out[ch][i] + stackLevel * b[ch][i]) * norm;
            }
        }
        else
        {
            stackIdle = true;
        }

        if (engage > 0.0f)
        {
            // Anti-chipmunk (clean engine only - in RAW the converter emulation darkens it)
            const float lpHz = 16000.0f / std::pow (ratioFor (sw::jmax (0.0f, sw::jmax (mainSemis, stackLevel > 0.0f ? stackSemis : 0.0f))), 0.9f);
            const float lpCoeff = mainVoice.isRaw() ? 0.0f : std::exp (-swarm::kTwoPi * lpHz / (float) sampleRate);

            for (int i = 0; i < n; ++i)
            {
                // Attack restoration: the shifted signal follows the dry envelope (bounded, fast attack)
                float dryA = 0.0f, wetA = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    dryA = sw::jmax (dryA, std::abs (dry.getSample (ch, i)));
                    wetA = sw::jmax (wetA, std::abs (out[ch][i]));
                }
                dryEnv += (dryA > dryEnv ? envAttack : envRelease) * (dryA - dryEnv);
                wetEnv += (wetA > wetEnv ? envAttack : envRelease) * (wetA - wetEnv);
                const float target = sw::jlimit (0.6f, 2.0f, (dryEnv + 1.0e-4f) / (wetEnv + 1.0e-4f));
                restoreGain += (target > restoreGain ? gainUp : gainDown) * (target - restoreGain);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& z = chipmunkLp[(size_t) ch];
                    z = out[ch][i] + lpCoeff * (z - out[ch][i]);
                    // sub-sonic cut: a down-shift on a low tuning would otherwise land at 20-30 Hz
                    out[ch][i] = subsonic[(size_t) ch].process (z * restoreGain);
                }
            }
        }
        else
        {
            restoreGain = 1.0f;
            dryEnv = wetEnv = 0.0f;
            chipmunkLp = { 0.0f, 0.0f };
            for (auto& f : subsonic) f.reset();
        }

        speed.process (out, numChannels, n);   // BUZZ
    }

    double sampleRate = 44100.0;
    Settings settings;

    ShiftRing ring;
    PitchVoice mainVoice, angerVoice, stackVoice;
    SpeedStage speed;
    sw::AudioBuffer<float> dry, shifted, angerBuf, stackBuf;
    swarm::FastRandom rng { 0x5EED1E5u };
    swarm::OnePole chaosSmoother;

    float currentSemis = 0.0f, targetSemis = 0.0f, rampStep = 0.0f, displaySemis = 0.0f;
    float stackSemis = 0.0f, stackTarget = 0.0f, stackStep = 0.0f, stackLevel = 0.0f;
    bool stackActive = false;
    float wet = 0.0f, angerLevel = 0.0f;
    float chaosPhase = 0.0f, chaosTarget = 0.0f, wobblePhase = 0.0f;
    float dryEnv = 0.0f, wetEnv = 0.0f, restoreGain = 1.0f;
    float envAttack = 0.05f, envRelease = 0.001f, gainUp = 0.02f, gainDown = 0.003f;
    std::array<float, 2> chipmunkLp {};
    std::array<swarm::SVF, 2> subsonic;
    bool idle = true, stackIdle = true;
    float lastMain = 1.0f, lastAnger = 1.0f, lastStack = 1.0f;
};
