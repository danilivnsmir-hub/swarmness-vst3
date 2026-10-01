#pragma once

#include "DSPUtils.h"
#include "PitchVoice.h"
#include "SpeedStage.h"
#include <array>
#include <functional>
#include <vector>

/**
 * HIVE - harmonies and pitch-shifting repeats (the footswitch shifter is its own block, SHIFT).
 *
 *  VOICES  DRONE (PITCH interval) and QUEEN (its octave) harmonise whatever reaches the block -
 *          after SHIFT in the chain they follow the shifted note, before it they do not.
 *          TRACKING: tight .. laggy with repeating grains.
 *  TRAILS  pitch-shifting regeneration (a climbing pitch delay), shaped by a STEP pattern like a
 *          pattern tremolo - but its steps are the repeats. Step k = repeat k+1 after the note
 *          (restarts on every picked note, or follows the host grid with SYNC). Each step has a
 *          LEVEL (0 = a silent repeat, the tail keeps running underneath) and a MOVE:
 *            HOLD    the repeat keeps its pitch (a plain echo)
 *            UP/DOWN the repeat moves by +PITCH / -PITCH (all UP = the classic ascending ladder)
 *            RANDOM  a random chord tone of PITCH or an octave (glitch arpeggios)
 *            REVERSE the repeat plays backwards
 *          GATE chops every step (0 = full repeats, high = short stuttering chops). DRY: the
 *          repeats start from the input instead of the DRONE - a plain or pitch-shifting delay. TIME = step /
 *          repeat spacing, TONE = brightness; VENOM drives the loop into self-oscillation (and
 *          switches HIVE on while held).
 *  MANGLE  on the voices and trails: ANGER (sour detuned voices), FRENZY (random pitch jumps),
 *          BUZZ (all-pass feedback + AM), RAW (cheap-pedal-DSP character), DETUNE (width) and
 *          MIX (dry / voices, pedal law).
 *
 * DRONE + QUEEN read one shared input ring, the trail voice its own ring. Idle voices are not
 * processed at all.
 */
class HiveBlock
{
public:
    static constexpr int kControlBlock = 32;
    static constexpr double kMaxRepeatSeconds = 2.0;
    static constexpr int kMaxSteps = 16;

    /** What a TRAILS step does to its repeat. */
    enum StepMove : int { hold = 0, up, down, random, reverse, numStepMoves };

    struct StepPattern
    {
        int numSteps = 8;
        std::array<float, kMaxSteps> level { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        std::array<int, kMaxSteps> move { up, up, up, up, up, up, up, up, up, up, up, up, up, up, up, up };
    };

    /** Ready-made step patterns (the first five match the old PATTERN choice, so it migrates 1:1). */
    enum Fill : int { fillLadder = 0, fillBounce, fillScatter, fillReverse, fillSwell, fillEcho, fillStutter,
                      fillGallop, fillOffbeat, fillGlitch, numFills };

    /**
     * MANGLE: one knob for the chaos of the voices and trails. It brings in ANGER first (sour
     * detuned voices), then FRENZY (random pitch jumps), then BUZZ (all-pass + AM) on top.
     */
    struct Mangle { float anger, frenzy, buzz; };
    static Mangle mangleFor (float m) noexcept
    {
        auto ramp = [m] (float from, float to) { return sw::jlimit (0.0f, 1.0f, (m - from) / (to - from)); };
        return { 0.6f * ramp (0.0f, 0.45f) + 0.3f * ramp (0.5f, 1.0f), 0.55f * ramp (0.3f, 0.8f), 0.6f * ramp (0.6f, 1.0f) };
    }
    /** The MANGLE setting closest to separate ANGER / FRENZY / BUZZ amounts (0..1). */
    static float mangleFromParts (float anger, float frenzy, float buzz) noexcept
    {
        const float a = sw::jmin (anger, 0.6f) / 0.6f * 0.45f;
        const float f = frenzy > 0.001f ? 0.3f + sw::jmin (frenzy, 0.55f) / 0.55f * 0.5f : 0.0f;
        const float b = buzz > 0.001f ? 0.6f + sw::jmin (buzz, 0.6f) / 0.6f * 0.4f : 0.0f;
        return sw::jlimit (0.0f, 1.0f, sw::jmax (a, sw::jmax (f, b)));
    }

    static StepPattern makeFill (int fill) noexcept
    {
        StepPattern p;
        auto set = [&p] (int n, std::initializer_list<float> levels, std::initializer_list<int> moves)
        {
            p.numSteps = n;
            int k = 0;
            for (auto l : levels) p.level[(size_t) k++ % kMaxSteps] = l;
            k = 0;
            for (auto m : moves) p.move[(size_t) k++ % kMaxSteps] = m;
        };
        switch (fill)
        {
            case fillBounce:  set (8, { 1, 1, 1, 1, 1, 1, 1, 1 }, { down, up, down, up, down, up, down, up }); break;
            case fillScatter: set (8, { 1, 1, 1, 1, 1, 1, 1, 1 }, { random, random, random, random, random, random, random, random }); break;
            case fillReverse: set (8, { 1, 1, 1, 1, 1, 1, 1, 1 }, { reverse, reverse, reverse, reverse, reverse, reverse, reverse, reverse }); break;
            case fillSwell:   set (8, { 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1, 1 }, { up, up, up, up, up, up, up, up }); break;
            case fillEcho:    set (8, { 1, 1, 1, 1, 1, 1, 1, 1 }, { hold, hold, hold, hold, hold, hold, hold, hold }); break;
            case fillStutter: set (8, { 1, 1, 1, 0, 1, 1, 0, 0 }, { hold, hold, up, hold, hold, down, hold, hold }); break;
            case fillGallop:  set (4, { 1, 0, 1, 1 }, { up, up, up, up }); break;
            case fillOffbeat: set (2, { 0, 1 }, { up, up }); break;
            case fillGlitch:  set (16, { 1, 0.8f, 0, 1, 0.6f, 0, 1, 0.7f, 1, 0, 0.5f, 1, 0, 1, 0.7f, 0.9f },
                                   { up, reverse, hold, random, hold, hold, down, random, reverse, hold, up, random, hold, hold, reverse, down }); break;
            case fillLadder:
            default: break;
        }
        return p;
    }

    struct Settings
    {
        // VOICES
        bool voicesOn = false, snap = true;
        float pitchSemis = 7.0f, drone = 0.6f, queen = 0.0f, tracking = 0.8f;

        // TRAILS
        float trails = 0.0f, repeatSeconds = 0.18f, tone = 0.6f;
        StepPattern steps;
        float gate = 1.0f;          // how much of every step sounds (1 = the whole repeat)
        bool fromDry = false;       // the repeats start from the input instead of the DRONE (a pitch delay)
        double hostStep = -1.0;     // SYNC: the host position in steps (< 0 = restart the pattern on every note)
        bool venom = false;

        // MANGLE
        float anger = 0.0f, frenzy = 0.0f, buzz = 0.0f, detuneCents = 0.0f, mix = 0.5f;
        bool raw = true;
    };

    /** Diagnostics: gets each stage's audio per control block ("voiceIn", "trailIn", "trail", "voicesOut"). Unset normally. */
    std::function<void (const char*, const float* const*, int, int)> debugTap;

    void prepare (double sr, int maxBlockSize)
    {
        (void) maxBlockSize;   // works in control blocks of 32 samples
        sampleRate = sr;

        for (auto* r : { &voiceRing, &trailRing })
            r->prepare (sr, 2);
        droneVoice.prepare (voiceRing);
        queenVoice.prepare (voiceRing);
        trailVoice.prepare (trailRing);
        for (auto* v : allVoices())
            v->setLoFi (26000.0f, 13.0f, 10000.0f);  // cheap converters, shared by every voice

        for (auto* b : { &dry, &voiceIn, &droneBuf, &queenBuf, &trailIn, &trailBuf, &voicesOut })
            b->setSize (2, kControlBlock, false, false, true);

        speedVoices.prepare (sr);
        chaosSmoother.setTime (sr / kControlBlock, 0.012);
        dryLevelCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
        onsetAttack  = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
        onsetRelease = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        onsetSlow    = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        stepOpen  = (float) (1.0 - std::exp (-1.0 / (0.0015 * sr)));
        stepClose = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
        reverseXfade = (float) (1.0 - std::exp (-1.0 / (0.006 * sr)));

        const int maxLag = (int) std::ceil (sr * 0.2) + 8;
        lagLine.setMaximumDelayInSamples (maxLag);
        lagLine.prepare ({ sr, (sw::uint32) kControlBlock, 2 });

        onSmoothed.reset (sr, 0.03);   droneLevel.reset (sr, 0.03);
        queenLevel.reset (sr, 0.03);   loopGain.reset (sr, 0.05);
        resonance.reset (sr, 0.05);    lagSmoothed.reset (sr, 0.15);
        voiceGain.reset (sr, 0.02);
        onSmoothed.setCurrentAndTargetValue (0.0f);
        lagSmoothed.setCurrentAndTargetValue (0.0f);
        voiceGain.setCurrentAndTargetValue (1.0f);

        loopSize = sw::nextPowerOfTwo ((int) std::ceil (sr * kMaxRepeatSeconds) + 64);
        loopMask = loopSize - 1;
        for (auto& b : loopBuf)
            b.assign ((size_t) loopSize, 0.0f);
        loopDelay.setTime (sr / kControlBlock, 0.12);
        loopDelay.reset (loopDelayTarget);

        for (int ch = 0; ch < 2; ++ch)
        {
            loopLp[(size_t) ch].setType (swarm::SVF::Type::lowPass);
            loopLp[(size_t) ch].setParams (sr, 3500.0f, 0.7071f);
            loopHp[(size_t) ch].setType (swarm::SVF::Type::highPass);
            loopHp[(size_t) ch].setParams (sr, 45.0f, 0.7071f);
            voiceSubsonic[(size_t) ch].setType (swarm::SVF::Type::highPass);
            voiceSubsonic[(size_t) ch].setParams (sr, 38.0f, 0.7071f);
        }
        const double controlRate = sr / kControlBlock;
        for (auto& d : driftCents)
            d.setTime (controlRate, 0.35);
        for (auto& d : dc) d.prepare (sr);
        reset();
    }

    void reset()
    {
        for (auto* r : { &voiceRing, &trailRing })
            r->clear();
        for (auto* v : allVoices())
            v->reset();
        speedVoices.reset();
        lagLine.reset();
        clearLoop();

        chaosPhase = chaosTarget = wobblePhase = 0.0f;
        chaosSmoother.reset (0.0f);
        queenIdle = trailIdle = true;
        lastDroneRatio = lastQueenRatio = lastTrailRatio = 1.0f;

        toneState = droneLp = queenLp = trailLp = loopTone = { 0.0f, 0.0f };
        for (auto& f : loopLp) f.reset();
        for (auto& f : loopHp) f.reset();
        for (auto& f : voiceSubsonic) f.reset();
        for (auto& d : dc) d.reset();
        for (auto& d : driftCents) d.reset (0.0f);
        driftTarget = { 0.0f, 0.0f, 0.0f };
        driftCounter = 0;
        fastEnv = slowEnv = 0.0f;
        onsetHoldoff = 0;
        trailPan = { 1.0f, 0.72f };
        resetSteps();
    }

    void setParams (const Settings& s) noexcept
    {
        settings = s;
        settings.steps.numSteps = sw::jlimit (1, kMaxSteps, s.steps.numSteps);
        settings.gate = sw::jlimit (0.05f, 1.0f, s.gate);

        for (auto* v : allVoices())
            v->setRaw (s.raw);
        // RAW: a vintage pitch-delay warble - deeper and slower as TRACKING goes down
        const float loose = 1.0f - s.tracking;
        droneVoice.setRawCharacter (7.0f + 10.0f * loose, 4.0f, 0.5f);
        queenVoice.setRawCharacter (9.0f + 12.0f * loose, 3.3f, 0.5f);
        trailVoice.setRawCharacter (7.0f + 10.0f * loose, 4.0f, 0.5f);
        for (auto* v : { &droneVoice, &queenVoice, &trailVoice })
            v->setTightness (s.tracking);

        const bool voicesActive = s.voicesOn || s.venom;
        onSmoothed.setTargetValue (voicesActive ? 1.0f : 0.0f);
        droneLevel.setTargetValue (s.drone);
        queenLevel.setTargetValue (s.queen);
        toneCoeff = std::exp (-swarm::kTwoPi * (400.0f * std::pow (40.0f, s.tone)) / (float) sampleRate);
        lagSmoothed.setTargetValue (loose * loose * 0.12f * (float) sampleRate);
        loopDelayTarget = (float) (sw::jlimit (0.02, kMaxRepeatSeconds - 0.01, (double) s.repeatSeconds) * sampleRate);

        // TRAILS: loop gain below one -> even, predictable decay. VENOM goes just past one: the
        // trails swell and sustain instead of shrieking (tanh-limited).
        loopGain.setTargetValue (s.venom ? 1.3f : 0.9f * std::pow (sw::jlimit (0.0f, 1.0f, s.trails), 0.8f));
        resonance.setTargetValue (s.venom ? 0.5f : 0.0f);

        // MIX (pedal law): 50% = dry and effect both full, 100% = effect only
        dryLevelTarget = voicesActive ? sw::jmin (1.0f, 2.0f * (1.0f - s.mix)) : 1.0f;
        voiceGain.setTargetValue (voicesActive ? sw::jmin (1.0f, 2.0f * s.mix) : 1.0f);

        speedVoices.setAmount (s.buzz);

        if (s.hostStep >= 0.0)
        {
            hostStepNow = s.hostStep;
            hostSynced = true;
        }
        else
        {
            hostSynced = false;
        }
    }

    /** The TRAILS step now playing, -1 while there are no trails (for the editor). */
    int getCurrentStep() const noexcept      { return trailIdle ? -1 : stepIndex; }

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
    std::array<PitchVoice*, 3> allVoices() noexcept { return { &droneVoice, &queenVoice, &trailVoice }; }

    static float ratioFor (float semis) noexcept { return std::pow (2.0f, semis / 12.0f); }

    //==========================================================================
    void processControlBlock (float* const* audio, int numChannels, int n) noexcept
    {
        const auto& s = settings;
        const float dt = (float) n / (float) sampleRate;

        // ---- MANGLE control: FRENZY random pitch targets, ANGER detune (+ slow wobble when high)
        chaosPhase += (2.0f + 16.0f * s.frenzy) * dt;
        if (chaosPhase >= 1.0f)
        {
            chaosPhase -= std::floor (chaosPhase);
            const float range = std::pow (s.frenzy, 1.5f) * 12.0f;
            if (s.snap)
            {
                // SNAP: jumps land on musical intervals (4th, 5th, octave) instead of in-between pitches
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
        const float chaos = chaosSmoother.advance (chaosTarget, 1);

        wobblePhase += 0.37f * dt;
        if (wobblePhase >= 1.0f) wobblePhase -= 1.0f;
        const float wobble = s.anger > 0.5f ? (s.anger - 0.5f) * 0.6f * std::sin (swarm::kTwoPi * wobblePhase) : 0.0f;
        const float angerSemis = std::pow (s.anger, 1.3f) * 1.5f + wobble;
        const float fine = s.detuneCents * 0.01f;

        for (int ch = 0; ch < numChannels; ++ch)
            dry.copyFrom (ch, 0, audio[ch], n);

        const bool voicesActive = onSmoothed.getCurrentValue() > 0.0f || onSmoothed.isSmoothing();
        if (voicesActive)
        {
            loopCleared = false;
            processVoices (numChannels, n, chaos, angerSemis, fine);
        }
        else if (! loopCleared)
        {
            clearLoop();
        }

        // ---- OUTPUT: dry (turned down by MIX) + voices
        for (int i = 0; i < n; ++i)
        {
            dryLevel += dryLevelCoeff * (dryLevelTarget - dryLevel);
            if (std::abs (dryLevel - dryLevelTarget) < 1.0e-6f)
                dryLevel = dryLevelTarget;
            const float g = voiceGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] = dryLevel * dry.getSample (ch, i) + (voicesActive ? g * voicesOut.getSample (ch, i) : 0.0f);
        }
    }

    //==========================================================================
    void processVoices (int numChannels, int n, float chaos, float anger, float fine) noexcept
    {
        const auto& s = settings;
        const float loopD = sw::jmax ((float) (kControlBlock + 2), loopDelay.process (loopDelayTarget));

        // Humanise: slow random pitch drift, independent per voice
        if (++driftCounter >= (int) (0.4 * sampleRate / kControlBlock))
        {
            driftCounter = 0;
            driftTarget = { 3.0f * rng.nextBipolar(), 6.0f * rng.nextBipolar(), 3.0f * rng.nextBipolar() };
        }
        const float pitch = s.snap ? std::round (s.pitchSemis) : s.pitchSemis;
        const float droneSemis = pitch + chaos + 0.5f * anger + fine + driftCents[0].process (driftTarget[0]) * 0.01f;
        const float queenSemis = pitch + (pitch >= 0.0f ? 12.0f : -12.0f) + chaos - 0.5f * anger - fine
                               + driftCents[1].process (driftTarget[1]) * 0.01f;
        const float droneRatio = ratioFor (droneSemis), queenRatio = ratioFor (queenSemis);

        // Anti-chipmunk for up-shifted voices (clean engine only)
        const bool raw = droneVoice.isRaw();
        const auto lpCoeff = [this, raw] (float ratio)
        {
            if (raw) return 0.0f;
            const float hz = 16000.0f / std::pow (sw::jmax (1.0f, ratio), 0.9f);
            return std::exp (-swarm::kTwoPi * hz / (float) sampleRate);
        };
        const float droneLpC = lpCoeff (droneRatio), queenLpC = lpCoeff (queenRatio);

        // Voice input: what reaches the block, lagged by TRACKING
        bool onset = false;
        for (int i = 0; i < n; ++i)
        {
            lagLine.setDelay (lagSmoothed.getNextValue());
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                lagLine.pushSample (ch, dry.getSample (ch, i));
                const float v = lagLine.popSample (ch);
                voiceIn.setSample (ch, i, v);
                peak = sw::jmax (peak, std::abs (v));
            }
            // Note onsets restart the step pattern: step 1 = the first repeat of this note
            fastEnv += (peak > fastEnv ? onsetAttack : onsetRelease) * (peak - fastEnv);
            slowEnv += onsetSlow * (fastEnv - slowEnv);
            if (onsetHoldoff > 0) --onsetHoldoff;
            else if (fastEnv > 0.01f && fastEnv > 2.0f * slowEnv + 0.002f)
            {
                onset = true;
                onsetHoldoff = (int) (0.08 * sampleRate);
            }
        }
        if (onset && ! hostSynced)
        {
            // the step before step 1 runs while the note itself plays
            stepPos = 0.0f;
            enterStep (s.steps.numSteps - 1);
        }
        advanceSteps (loopD);
        const float trailSemis = stepSemis + driftCents[2].process (driftTarget[2]) * 0.01f;
        const float trailRatio = ratioFor (trailSemis);
        const float trailLpC = lpCoeff (droneRatio * trailRatio);

        float* vin[2] = { voiceIn.getWritePointer (0), voiceIn.getWritePointer (1) };
        const int voiceBase = voiceRing.write (vin, numChannels, n);
        if (debugTap) debugTap ("voiceIn", vin, numChannels, n);

        float* drone[2] = { droneBuf.getWritePointer (0), droneBuf.getWritePointer (1) };
        droneVoice.process (drone, numChannels, n, voiceBase, lastDroneRatio, droneRatio);
        lastDroneRatio = droneRatio;

        float* queen[2] = { queenBuf.getWritePointer (0), queenBuf.getWritePointer (1) };
        if (s.queen > 0.0f || queenLevel.isSmoothing() || queenLevel.getCurrentValue() > 0.0f)
        {
            if (queenIdle) { queenVoice.reset(); lastQueenRatio = queenRatio; queenIdle = false; }
            queenVoice.process (queen, numChannels, n, voiceBase, lastQueenRatio, queenRatio);
            lastQueenRatio = queenRatio;
        }
        else
        {
            queenIdle = true;
            queenBuf.clear();
        }

        // ---- TRAILS: the loop (delayed voices), read per step (forwards / backwards), into the trail shifter
        float* trail[2] = { trailBuf.getWritePointer (0), trailBuf.getWritePointer (1) };
        const bool trailsRunning = loopGain.getTargetValue() > 0.0f || loopGain.isSmoothing() || loopGain.getCurrentValue() > 0.0f;
        if (trailsRunning)
        {
            // Tape-like wow & flutter on the repeats (deeper with RAW): they drift and breathe instead of
            // coming back sample-exact.
            const float wowRate = 0.55f, flutterRate = 6.3f;
            const float raw01 = settings.raw ? 1.0f : 0.5f;
            const float wowDepth = raw01 * 0.0045f * (float) sampleRate / (swarm::kTwoPi * wowRate);        // ~ +-8 ct
            const float flutterDepth = raw01 * 0.0015f * (float) sampleRate / (swarm::kTwoPi * flutterRate); // ~ +-2.5 ct
            for (int i = 0; i < n; ++i)
            {
                wowPhase += wowRate / (float) sampleRate;
                flutterPhase += flutterRate / (float) sampleRate;
                if (wowPhase >= 1.0f) wowPhase -= 1.0f;
                if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
                const float mod = wowDepth * std::sin (swarm::kTwoPi * wowPhase) + flutterDepth * std::sin (swarm::kTwoPi * flutterPhase);
                const float g = loopGain.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float fb = readSteps (ch, i, loopD + mod);
                    trailIn.setSample (ch, i, g * 0.5f * std::tanh (2.0f * fb));
                }
                advanceReverse (loopD);
                reverseMix += reverseXfade * (reverseTarget - reverseMix);
            }
            float* tin[2] = { trailIn.getWritePointer (0), trailIn.getWritePointer (1) };
            const int trailBase = trailRing.write (tin, numChannels, n);
            if (debugTap) debugTap ("trailIn", tin, numChannels, n);
            if (trailIdle) { trailVoice.reset(); lastTrailRatio = trailRatio; trailIdle = false; }
            trailVoice.process (trail, numChannels, n, trailBase, lastTrailRatio, trailRatio);
            lastTrailRatio = trailRatio;
            if (debugTap)
            {
                debugTap ("trail", trail, numChannels, n);
                // the shifter's state, one value per sample: delay in ms | fading*1000 + unity*100 + ratio cents
                float st0[kControlBlock], st1[kControlBlock];
                const auto& e = trailVoice.engine();
                for (int i = 0; i < n; ++i)
                {
                    st0[i] = e.currentDelay() * 10.0f / (float) sampleRate;                      // delay in ms / 100
                    st1[i] = ((e.isFading() ? 1000.0f : 0.0f) + 100.0f * e.unityAmount() + 1200.0f * std::log2 (trailRatio)) * 1.0e-4f;
                }
                const float* st[2] { st0, st1 };
                debugTap ("trailState", st, 2, n);
            }
        }
        else
        {
            trailIdle = true;
            trailBuf.clear();
        }

        // ---- mix the voices, feed the loop
        const float gateLength = s.gate >= 0.999f ? 1.0e9f : s.gate * loopD;
        for (int i = 0; i < n; ++i)
        {
            for (size_t c = 0; c < 2; ++c)
                trailPan[c] += 0.004f * (trailPanTarget[c] - trailPan[c]);
            // the step's LEVEL, chopped by GATE (only what you hear - the tail keeps running)
            const float stepTarget = stepPos + (float) i < gateLength ? stepLevel : 0.0f;
            stepGain += (stepTarget > stepGain ? stepOpen : stepClose) * (stepTarget - stepGain);

            const float on = onSmoothed.getNextValue();
            const float dl = droneLevel.getNextValue();
            const float ql = queenLevel.getNextValue();
            const float res = resonance.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& dlp = droneLp[(size_t) ch];
                auto& qlp = queenLp[(size_t) ch];
                auto& tlp = trailLp[(size_t) ch];
                const float d = drone[ch][i], q = queen[ch][i], t = trail[ch][i];
                dlp = d + droneLpC * (dlp - d);
                qlp = q + queenLpC * (qlp - q);
                tlp = t + trailLpC * (tlp - t);

                // Stereo placement: DRONE (and its trails) slightly left, QUEEN slightly right
                const bool right = numChannels > 1 && ch == 1;
                const float dPan = right ? 0.72f : 1.0f;
                const float qPan = numChannels > 1 && ! right ? 0.72f : 1.0f;

                // Repeats: placed per step (UP left, DOWN right - a ping-pong; RANDOM jumps around)
                const float tPan = numChannels > 1 ? trailPan[(size_t) ch] : 1.0f;
                const float trailLevel = s.fromDry ? 1.0f : dl;
                const float voices = voiceSubsonic[(size_t) ch].process (dlp * dPan * dl + tlp * tPan * stepGain * trailLevel + qlp * ql * qPan);
                auto& z = toneState[(size_t) ch];
                z = voices + toneCoeff * (z - voices);

                // Loop: DRONE + its trails, TONE-filtered and band-limited (spiral ceiling).
                // The unshifted resonance only exists while VENOM is held.
                const float fbIn = (s.fromDry ? voiceIn.getSample (ch, i) : dlp) + tlp
                                 + res * (voiceIn.getSample (ch, i) + (trailsRunning ? trailIn.getSample (ch, i) : 0.0f));
                auto& lt = loopTone[(size_t) ch];
                lt = fbIn + toneCoeff * (lt - fbIn);
                const float banded = loopHp[(size_t) ch].process (loopLp[(size_t) ch].process (lt));
                loopBuf[(size_t) ch][(size_t) ((loopWrite + i) & loopMask)] = dc[(size_t) ch].process (banded);

                voicesOut.setSample (ch, i, on * 0.9f * std::tanh (z * (1.0f / 0.9f)));
            }
        }
        loopWrite = (loopWrite + n) & loopMask;
        stepPos += (float) n;
        if (hostSynced)
            hostStepNow += (double) n / (double) loopDelayTarget;

        float* vo[2] = { voicesOut.getWritePointer (0), voicesOut.getWritePointer (1) };
        speedVoices.process (vo, numChannels, n);   // BUZZ
        if (debugTap) debugTap ("voicesOut", vo, numChannels, n);
    }

    //==========================================================================
    // STEPS

    void resetSteps() noexcept
    {
        stepIndex = 0;
        stepMove = settings.steps.move[0];
        stepPos = 0.0f;
        stepSemis = 0.0f;
        stepLevel = 1.0f;
        stepGain = 0.0f;
        reversePhase = 0.0f;
        reverseTarget = reverseMix = 0.0f;
        trailPanTarget = { 1.0f, 0.72f };
        randomAt = 0.0f;
    }

    /** Called at the start of every control block: moves to the next step when the repeat is over. */
    void advanceSteps (float loopD) noexcept
    {
        const int numSteps = settings.steps.numSteps;
        if (hostSynced)
        {
            // SYNC: the step follows the host grid (step length = the tempo division)
            const double whole = std::floor (hostStepNow);
            const int k = (int) (((long long) whole % numSteps + numSteps) % numSteps);
            stepPos = (float) ((hostStepNow - whole) * (double) loopD);
            if (k != stepIndex)
                enterStep (k);
        }
        else
        {
            if (stepIndex >= numSteps)
                enterStep (0);
            while (stepPos >= loopD)
            {
                stepPos -= loopD;
                enterStep ((stepIndex + 1) % numSteps);
            }
        }
        followPitch();
    }

    void enterStep (int k) noexcept
    {
        const auto& st = settings.steps;
        stepIndex = sw::jlimit (0, st.numSteps - 1, k);
        stepLevel = sw::jlimit (0.0f, 1.0f, st.level[(size_t) stepIndex]);
        stepMove = st.move[(size_t) stepIndex];
        const float pitch = currentPitch();
        reverseTarget = 0.0f;
        switch (stepMove)
        {
            case hold:
                stepSemis = 0.0f;
                trailPanTarget = { 1.0f, 0.85f };
                break;
            case down:
                stepSemis = -pitch;
                trailPanTarget = { 0.45f, 1.0f };
                break;
            case random:
            {
                // a chord tone of PITCH or an octave, never the same as last time
                const float options[] { pitch, -pitch, 12.0f, -12.0f };
                float next = randomAt;
                for (int tries = 0; tries < 6 && std::abs (next - randomAt) < 0.01f; ++tries)
                    next = options[(size_t) (rng.nextInt() % 4u)];
                if (std::abs (next) < 0.01f)
                    next = 12.0f;
                stepSemis = next;
                randomAt = next;
                const float pan = rng.nextBipolar();
                trailPanTarget = { sw::jmin (1.0f, 1.0f - pan), sw::jmin (1.0f, 1.0f + pan) };
                break;
            }
            case reverse:
                stepSemis = 0.0f;
                reverseTarget = 1.0f;
                trailPanTarget = { 1.0f, 1.0f };
                break;
            case up:
            default:
                stepSemis = pitch;
                trailPanTarget = { 1.0f, 0.45f };
                break;
        }
    }

    float currentPitch() const noexcept { return settings.snap ? std::round (settings.pitchSemis) : settings.pitchSemis; }

    /** UP / DOWN follow PITCH while the step plays (turning the knob is heard at once). */
    void followPitch() noexcept
    {
        if (stepMove == up)   stepSemis = currentPitch();
        if (stepMove == down) stepSemis = -currentPitch();
    }

    float readSteps (int ch, int i, float loopD) const noexcept
    {
        const float now = (float) (loopWrite + i);
        const float forward = readLoop (ch, now - loopD);
        if (reverseMix < 1.0e-4f && reverseTarget < 0.5f)
            return forward;

        // REVERSE: two heads run backwards through the last window, crossfaded (sin^2)
        const float w = reverseWindow (loopD);
        float backward = 0.0f;
        for (int head = 0; head < 2; ++head)
        {
            float ph = reversePhase + (head == 0 ? 0.0f : 0.5f * w);
            if (ph >= w) ph -= w;
            const float gain = std::sin (swarm::kPi * ph / w);
            backward += gain * gain * readLoop (ch, now - w - 2.0f * ph);
        }
        return forward + reverseMix * (backward - forward);
    }

    void advanceReverse (float loopD) noexcept
    {
        const float w = reverseWindow (loopD);
        reversePhase += 1.0f;
        if (reversePhase >= w)
            reversePhase -= w;
    }

    float reverseWindow (float loopD) const noexcept
    {
        return sw::jlimit ((float) kControlBlock * 2.0f, (float) loopSize / 3.2f, loopD);
    }

    //==========================================================================
    void clearLoop() noexcept
    {
        for (auto& b : loopBuf)
            std::fill (b.begin(), b.end(), 0.0f);
        loopCleared = true;
        resetSteps();
    }

    float readLoop (int ch, float pos) const noexcept
    {
        const float fl = std::floor (pos);
        const int i0 = (int) fl;
        const float frac = pos - fl;
        const auto& b = loopBuf[(size_t) ch];
        const float a = b[(size_t) (i0 & loopMask)];
        return a + frac * (b[(size_t) ((i0 + 1) & loopMask)] - a);
    }

    //==========================================================================
    double sampleRate = 44100.0;
    Settings settings;

    ShiftRing voiceRing, trailRing;
    PitchVoice droneVoice, queenVoice, trailVoice;
    SpeedStage speedVoices;
    sw::AudioBuffer<float> dry, voiceIn, droneBuf, queenBuf, trailIn, trailBuf, voicesOut;
    sw::dsp::DelayLine<float, sw::dsp::DelayLineInterpolationTypes::Linear> lagLine { 1 };
    sw::SmoothedValue<float> onSmoothed, droneLevel, queenLevel, loopGain, resonance, lagSmoothed, voiceGain;
    swarm::FastRandom rng { 0x5EED1E5u };
    swarm::OnePole chaosSmoother, loopDelay;
    std::array<swarm::OnePole, 3> driftCents;
    std::array<float, 3> driftTarget {};
    int driftCounter = 0;

    float chaosPhase = 0.0f, chaosTarget = 0.0f, wobblePhase = 0.0f;
    float dryLevel = 1.0f, dryLevelTarget = 1.0f, dryLevelCoeff = 0.001f;
    bool queenIdle = true, trailIdle = true;
    float lastDroneRatio = 1.0f, lastQueenRatio = 1.0f, lastTrailRatio = 1.0f;

    // VOICES / TRAILS
    std::array<std::vector<float>, 2> loopBuf;
    int loopSize = 0, loopMask = 0, loopWrite = 0;
    bool loopCleared = false;
    float loopDelayTarget = 8000.0f, toneCoeff = 0.0f;
    std::array<float, 2> toneState {}, droneLp {}, queenLp {}, trailLp {}, loopTone {};
    std::array<swarm::SVF, 2> loopLp, loopHp, voiceSubsonic;
    std::array<swarm::DCBlocker, 2> dc;

    // STEPS
    int stepIndex = 0, stepMove = up;
    float stepPos = 0.0f, stepSemis = 0.0f, stepLevel = 1.0f, stepGain = 0.0f, randomAt = 0.0f;
    float stepOpen = 0.01f, stepClose = 0.005f;
    double hostStepNow = 0.0;
    bool hostSynced = false;
    float reversePhase = 0.0f, reverseTarget = 0.0f, reverseMix = 0.0f, reverseXfade = 0.004f;
    float fastEnv = 0.0f, slowEnv = 0.0f, onsetAttack = 0.3f, onsetRelease = 0.001f, onsetSlow = 0.0005f;
    int onsetHoldoff = 0;
    float wowPhase = 0.0f, flutterPhase = 0.37f;
    std::array<float, 2> trailPan { 1.0f, 0.72f }, trailPanTarget { 1.0f, 0.72f };
};
