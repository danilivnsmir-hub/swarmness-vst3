#pragma once

#include "DSPUtils.h"
#include "PitchVoice.h"
#include "SpeedStage.h"
#include <array>
#include <vector>

/**
 * HIVE - the destructive pitch-delay. One block, four sections:
 *
 *  SHIFT   footswitches SHIFT A / SHIFT B transpose the played signal (any interval, -24..+24 st),
 *          RISE / FALL glide in and out (Whammy / Tallon "The Noise" style), BLEND = how much of the
 *          shifted signal replaces the dry one (100% = only the shifted note, 50% = doubled).
 *  VOICES  DRONE (PITCH interval) and QUEEN (its octave) harmonise the played note - or the
 *          shifted one with FOLLOW. TRACKING: tight .. laggy with repeating grains.
 *  TRAILS  pitch-shifting regeneration (Rainbow Machine style). PATTERN sets how the interval
 *          evolves repeat after repeat:
 *            LADDER  every repeat moves by PITCH again (spirals up / down)
 *            BOUNCE  repeats alternate between the voice and the played note (a trill)
 *            SCATTER every repeat jumps to a random chord tone of PITCH (glitch arpeggios)
 *            REVERSE repeats play backwards (and still move by PITCH)
 *            BLOOM   repeats are diffused and drift apart - a swelling cloud instead of steps
 *          TIME = spacing, TONE = brightness; the VENOM footswitch drives the loop into
 *          self-oscillation.
 *  MANGLE  shared by everything the block adds: ANGER (sour detuned second voices), FRENZY
 *          (random pitch jumps), BUZZ (all-pass feedback + AM), RAW (cheap-pedal-DSP character),
 *          DETUNE (fine offset / spread) and MIX (dry / effect, pedal law).
 *
 * Shifters share input rings: SHIFT + ANGER read the dry ring, DRONE + QUEEN the voice ring,
 * the trail voice its own ring. Idle voices are not processed at all.
 */
class HiveBlock
{
public:
    static constexpr int kControlBlock = 32;
    static constexpr double kMaxRepeatSeconds = 2.0;

    enum Pattern : int { ladder = 0, bounce, scatter, reverse, bloom, numPatterns };

    struct Settings
    {
        // SHIFT (footswitches already combined with LINK by the caller)
        bool shiftA = false, shiftB = false;
        float shiftASemis = 12.0f, shiftBSemis = 24.0f;
        float riseMs = 30.0f, fallMs = 30.0f, blend = 1.0f;

        // VOICES
        bool voicesOn = false, snap = true, follow = false;
        float pitchSemis = 7.0f, drone = 0.6f, queen = 0.0f, tracking = 0.8f;

        // TRAILS
        float trails = 0.0f, repeatSeconds = 0.18f, tone = 0.6f;
        int pattern = ladder;
        bool venom = false;

        // MANGLE
        float anger = 0.0f, frenzy = 0.0f, buzz = 0.0f, detuneCents = 0.0f, mix = 0.5f;
        bool raw = true;
    };

    void prepare (double sr, int maxBlockSize)
    {
        (void) maxBlockSize;   // works in control blocks of 32 samples
        sampleRate = sr;

        for (auto* r : { &dryRing, &voiceRing, &trailRing })
            r->prepare (sr, 2);
        for (auto* v : { &shiftVoice, &angerVoice })
        {
            v->prepare (dryRing);
            v->setRawCharacter (5.0f, 7.0f, 0.0f);   // RAW: slight warble; splices stay tight (footswitch attack)
        }
        droneVoice.prepare (voiceRing);
        queenVoice.prepare (voiceRing);
        trailVoice.prepare (trailRing);
        for (auto* v : allVoices())
            v->setLoFi (26000.0f, 13.0f, 10000.0f);  // cheap converters, shared by every voice

        for (auto* b : { &dry, &mainOut, &followIn, &shifted, &angerBuf, &voiceIn, &droneBuf, &queenBuf, &trailIn, &trailBuf, &voicesOut })
            b->setSize (2, kControlBlock, false, false, true);

        speedMain.prepare (sr);
        speedVoices.prepare (sr);
        chaosSmoother.setTime (sr / kControlBlock, 0.012);
        dryLevelCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
        envAttack  = (float) (1.0 - std::exp (-1.0 / (0.0005 * sr)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.025 * sr)));
        gainUp     = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
        gainDown   = (float) (1.0 - std::exp (-1.0 / (0.008 * sr)));
        onsetAttack  = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
        onsetRelease = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        onsetSlow    = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        bloomRelease = (float) (1.0 - std::exp (-1.0 / (0.25 * sr)));
        bloomSwell   = (float) (1.0 - std::exp (-1.0 / (0.6 * sr)));

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
            for (auto* f : { &voiceSubsonic[(size_t) ch], &shiftSubsonic[(size_t) ch] })
            {
                f->setType (swarm::SVF::Type::highPass);
                f->setParams (sr, 38.0f, 0.7071f);
            }
        }
        // BLOOM diffusers (ms, slightly different per side)
        static constexpr float diffMs[2][kDiffusers] { { 5.3f, 7.9f, 11.3f, 16.1f }, { 5.9f, 8.3f, 12.1f, 15.7f } };
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < kDiffusers; ++k)
                diffusers[(size_t) ch][(size_t) k].prepare ((int) (diffMs[ch][k] * 0.001 * sr));

        const double controlRate = sr / kControlBlock;
        for (auto& d : driftCents)
            d.setTime (controlRate, 0.35);
        for (auto& d : dc) d.prepare (sr);
        reset();
    }

    void reset()
    {
        for (auto* r : { &dryRing, &voiceRing, &trailRing })
            r->clear();
        for (auto* v : allVoices())
            v->reset();
        speedMain.reset();
        speedVoices.reset();
        lagLine.reset();
        clearLoop();

        currentSemis = targetSemis = rampStep = 0.0f;
        wet = 0.0f;
        angerLevel = 0.0f;
        chaosPhase = chaosTarget = wobblePhase = 0.0f;
        chaosSmoother.reset (0.0f);
        restoreGain = 1.0f;
        dryEnv = wetEnv = 0.0f;
        chipmunkLp = { 0.0f, 0.0f };
        shiftIdle = queenIdle = trailIdle = true;
        lastMainRatio = lastAngerRatio = lastDroneRatio = lastQueenRatio = lastTrailRatio = 1.0f;

        toneState = droneLp = queenLp = trailLp = loopTone = { 0.0f, 0.0f };
        for (auto& f : loopLp) f.reset();
        for (auto& f : loopHp) f.reset();
        for (auto& f : voiceSubsonic) f.reset();
        for (auto& f : shiftSubsonic) f.reset();
        for (auto& d : dc) d.reset();
        for (auto& d : driftCents) d.reset (0.0f);
        driftTarget = { 0.0f, 0.0f, 0.0f };
        driftCounter = 0;
        fastEnv = slowEnv = 0.0f;
        onsetHoldoff = 0;
        bloomEnv = 0.0f;
        bloomGain = 1.0f;
        trailPan = { 1.0f, 0.72f };
        resetPattern();
    }

    void setParams (const Settings& s) noexcept
    {
        settings = s;
        settings.pattern = sw::jlimit (0, numPatterns - 1, s.pattern);

        for (auto* v : allVoices())
            v->setRaw (s.raw);
        // RAW: the Rainbow-Machine-style warble - deeper and slower as TRACKING goes down
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

        // MIX (pedal law): 50% = dry and effect both full, 100% = effect only. It only turns the dry
        // part down, so a held SHIFT still sounds on top.
        dryLevelTarget = voicesActive ? sw::jmin (1.0f, 2.0f * (1.0f - s.mix)) : 1.0f;
        voiceGain.setTargetValue (voicesActive ? sw::jmin (1.0f, 2.0f * s.mix) : 1.0f);

        speedMain.setAmount (s.buzz);
        speedVoices.setAmount (s.buzz);

        if (settings.pattern != activePattern)
        {
            activePattern = settings.pattern;
            resetPattern();
        }

        // SHIFT target: B wins over A (like +2 OCT over +1 OCT)
        const float semis = s.shiftB ? s.shiftBSemis : (s.shiftA ? s.shiftASemis : 0.0f);
        if (! sw::exactlyEqual (semis, targetSemis))
        {
            // Moving away from home uses RISE, returning home (released) uses FALL.
            const bool returning = std::abs (semis) < std::abs (targetSemis) || std::abs (semis) < 0.001f;
            targetSemis = semis;
            const float ms = returning ? s.fallMs : s.riseMs;
            rampStep = std::abs (targetSemis - currentSemis) / sw::jmax (1.0f, ms * 0.001f * (float) sampleRate);
        }
    }

    bool isShiftEngaged() const noexcept    { return std::abs (targetSemis) > 0.001f || std::abs (currentSemis) > 0.001f; }
    float getShiftSemitones() const noexcept { return displaySemis; }

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
    static constexpr int kDiffusers = 4;

    std::array<PitchVoice*, 5> allVoices() noexcept { return { &shiftVoice, &angerVoice, &droneVoice, &queenVoice, &trailVoice }; }

    static float ratioFor (float semis) noexcept { return std::pow (2.0f, semis / 12.0f); }

    //==========================================================================
    void processControlBlock (float* const* audio, int numChannels, int n) noexcept
    {
        const auto& s = settings;
        const float dt = (float) n / (float) sampleRate;

        // ---- SHIFT glide (linear in semitones)
        const float stepThisBlock = rampStep * (float) n;
        if (currentSemis < targetSemis) currentSemis = sw::jmin (targetSemis, currentSemis + stepThisBlock);
        else                            currentSemis = sw::jmax (targetSemis, currentSemis - stepThisBlock);
        const bool engaged = isShiftEngaged();
        const float engage = engaged ? 1.0f : 0.0f;

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

        // ================================================================ SHIFT
        const int dryBase = dryRing.write (audio, numChannels, n);
        const bool shiftRunning = engaged || wet > 1.0e-5f;
        if (shiftRunning)
            processShift (numChannels, n, dryBase, engage, chaos * engage, angerSemis * engage, fine * engage);
        else
            shiftIdle = true;
        displaySemis = engaged ? currentSemis + chaos + 0.5f * angerSemis + fine : 0.0f;

        // main = dry / shifted (BLEND while engaged), with MIX turning only the dry part down
        for (int i = 0; i < n; ++i)
        {
            wet += 0.004f * (engage * s.blend - wet);
            if (! engaged && wet < 1.0e-5f)
                wet = 0.0f;
            dryLevel += dryLevelCoeff * (dryLevelTarget - dryLevel);
            if (std::abs (dryLevel - dryLevelTarget) < 1.0e-6f)
                dryLevel = dryLevelTarget;

            float dryG, wetG;
            swarm::equalPowerGains (wet, dryG, wetG);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float sh = shiftRunning ? wetG * shifted.getSample (ch, i) : 0.0f;
                followIn.setSample (ch, i, dryG * dry.getSample (ch, i) + sh);              // before MIX
                mainOut.setSample (ch, i, dryG * dryLevel * dry.getSample (ch, i) + sh);
            }
        }

        // ================================================================ VOICES + TRAILS
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

        // ================================================================ OUTPUT
        for (int i = 0; i < n; ++i)
        {
            const float g = voiceGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] = mainOut.getSample (ch, i) + (voicesActive ? g * voicesOut.getSample (ch, i) : 0.0f);
        }
    }

    //==========================================================================
    void processShift (int numChannels, int n, int base, float engage, float chaos, float anger, float fine) noexcept
    {
        const auto& s = settings;
        if (shiftIdle)
        {
            shiftVoice.reset();
            angerVoice.reset();
            speedMain.reset();
            lastMainRatio = lastAngerRatio = 1.0f;
            shiftIdle = false;
        }

        const float mainSemis  = currentSemis + chaos + 0.5f * anger + fine;
        const float angerVSemis = currentSemis + chaos - 0.5f * anger + fine;
        const float mainRatio = ratioFor (mainSemis), angerRatio = ratioFor (angerVSemis);

        float* out[2] = { shifted.getWritePointer (0), shifted.getWritePointer (1) };
        shiftVoice.process (out, numChannels, n, base, lastMainRatio, mainRatio);
        lastMainRatio = mainRatio;

        // ANGER: a second voice detuned the other way
        if (s.anger > 0.001f || angerLevel > 0.001f)
        {
            float* a[2] = { angerBuf.getWritePointer (0), angerBuf.getWritePointer (1) };
            angerVoice.process (a, numChannels, n, base, lastAngerRatio, angerRatio);
            lastAngerRatio = angerRatio;
            const float targetLevel = sw::jmin (1.0f, s.anger * 2.5f) * 0.85f;
            for (int i = 0; i < n; ++i)
            {
                angerLevel += 0.002f * (targetLevel - angerLevel);
                const float norm = 1.0f / (1.0f + 0.45f * angerLevel);
                for (int ch = 0; ch < numChannels; ++ch)
                    out[ch][i] = (out[ch][i] + angerLevel * a[ch][i]) * norm;
            }
        }

        if (engage > 0.0f)
        {
            // Anti-chipmunk (clean engine only - in RAW the converter emulation darkens it)
            const float lpHz = 16000.0f / std::pow (ratioFor (sw::jmax (0.0f, mainSemis)), 0.9f);
            const float lpCoeff = shiftVoice.isRaw() ? 0.0f : std::exp (-swarm::kTwoPi * lpHz / (float) sampleRate);

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
                    out[ch][i] = shiftSubsonic[(size_t) ch].process (z * restoreGain);
                }
            }
        }
        else
        {
            restoreGain = 1.0f;
            dryEnv = wetEnv = 0.0f;
            chipmunkLp = { 0.0f, 0.0f };
            for (auto& f : shiftSubsonic) f.reset();
        }

        speedMain.process (out, numChannels, n);   // BUZZ
    }

    //==========================================================================
    void processVoices (int numChannels, int n, float chaos, float anger, float fine) noexcept
    {
        const auto& s = settings;
        const float loopD = sw::jmax ((float) (kControlBlock + 2), loopDelay.process (loopDelayTarget * timeJitter));

        // Humanise: slow random pitch drift, independent per voice (BLOOM drifts much further)
        if (++driftCounter >= (int) (0.4 * sampleRate / kControlBlock))
        {
            driftCounter = 0;
            const float trailDrift = activePattern == bloom ? 18.0f : 3.0f;
            driftTarget = { 3.0f * rng.nextBipolar(), 6.0f * rng.nextBipolar(), trailDrift * rng.nextBipolar() };
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

        // Voice input: the played note (lagged by TRACKING) - or the shifted one with FOLLOW
        bool onset = false;
        for (int i = 0; i < n; ++i)
        {
            lagLine.setDelay (lagSmoothed.getNextValue());
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                lagLine.pushSample (ch, s.follow ? followIn.getSample (ch, i) : dry.getSample (ch, i));
                const float v = lagLine.popSample (ch);
                voiceIn.setSample (ch, i, v);
                peak = sw::jmax (peak, std::abs (v));
            }
            // Note onsets re-align the PATTERN's repeats (BOUNCE / SCATTER switch exactly between repeats)
            fastEnv += (peak > fastEnv ? onsetAttack : onsetRelease) * (peak - fastEnv);
            slowEnv += onsetSlow * (fastEnv - slowEnv);
            if (onsetHoldoff > 0) --onsetHoldoff;
            else if (fastEnv > 0.01f && fastEnv > 2.0f * slowEnv + 0.002f)
            {
                onset = true;
                onsetHoldoff = (int) (0.08 * sampleRate);
            }
        }
        if (onset && (activePattern == bounce || activePattern == scatter))
        {
            passPos = 0.0f;
            bounceUp = false;
            scatterAt = s.snap ? std::round (s.pitchSemis) : s.pitchSemis;   // the DRONE of this note arrives first
            scatterPrimed = true;
            patternStarted = true;
        }
        advancePattern (n, loopD);
        const float trailSemis = patternSemis + driftCents[2].process (driftTarget[2]) * 0.01f;
        const float trailRatio = ratioFor (trailSemis);
        const float trailLpC = lpCoeff (droneRatio * trailRatio);

        float* vin[2] = { voiceIn.getWritePointer (0), voiceIn.getWritePointer (1) };
        const int voiceBase = voiceRing.write (vin, numChannels, n);

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

        // ---- TRAILS: the loop (delayed voices), read per PATTERN, into the trail shifter
        float* trail[2] = { trailBuf.getWritePointer (0), trailBuf.getWritePointer (1) };
        const bool trailsRunning = loopGain.getTargetValue() > 0.0f || loopGain.isSmoothing() || loopGain.getCurrentValue() > 0.0f;
        if (trailsRunning)
        {
            // Tape-like wow & flutter on the repeats (deeper with RAW): they drift and breathe instead of
            // coming back sample-exact. SCATTER also jitters the repeat time from pass to pass.
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
                    float fb = readPattern (ch, i, loopD + mod);
                    if (activePattern == bloom)
                        for (auto& d : diffusers[(size_t) ch])
                            fb = d.process (fb, 0.62f);
                    trailIn.setSample (ch, i, g * 0.5f * std::tanh (2.0f * fb));
                }
                advanceReverse (loopD);
            }
            float* tin[2] = { trailIn.getWritePointer (0), trailIn.getWritePointer (1) };
            const int trailBase = trailRing.write (tin, numChannels, n);
            if (trailIdle) { trailVoice.reset(); lastTrailRatio = trailRatio; trailIdle = false; }
            trailVoice.process (trail, numChannels, n, trailBase, lastTrailRatio, trailRatio);
            lastTrailRatio = trailRatio;
        }
        else
        {
            trailIdle = true;
            trailBuf.clear();
        }

        // ---- mix the voices, feed the loop
        for (int i = 0; i < n; ++i)
        {
            for (size_t c = 0; c < 2; ++c)
                trailPan[c] += 0.0015f * (trailPanTarget[c] - trailPan[c]);
            float bloomTarget = 1.0f;
            if (activePattern == bloom)
            {
                float in = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    in = sw::jmax (in, std::abs (voiceIn.getSample (ch, i)));
                bloomEnv += (in > bloomEnv ? 0.01f : bloomRelease) * (in - bloomEnv);
                bloomTarget = 1.0f / (1.0f + 14.0f * bloomEnv);
            }
            bloomGain += (bloomTarget < bloomGain ? 0.01f : bloomSwell) * (bloomTarget - bloomGain);

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

                // Repeats: placed per PATTERN (BOUNCE ping-pongs, SCATTER jumps around), BLOOM ducks them
                // while you play so they swell up in the gaps
                const float tPan = numChannels > 1 ? trailPan[(size_t) ch] : 1.0f;
                const float voices = voiceSubsonic[(size_t) ch].process ((dlp * dPan + tlp * tPan * bloomGain) * dl + qlp * ql * qPan);
                auto& z = toneState[(size_t) ch];
                z = voices + toneCoeff * (z - voices);

                // Loop: DRONE + its trails, TONE-filtered and band-limited (spiral ceiling).
                // The unshifted resonance only exists while VENOM is held.
                const float fbIn = dlp + tlp + res * (voiceIn.getSample (ch, i) + (trailsRunning ? trailIn.getSample (ch, i) : 0.0f));
                auto& lt = loopTone[(size_t) ch];
                lt = fbIn + toneCoeff * (lt - fbIn);
                const float banded = loopHp[(size_t) ch].process (loopLp[(size_t) ch].process (lt));
                loopBuf[(size_t) ch][(size_t) ((loopWrite + i) & loopMask)] = dc[(size_t) ch].process (banded);

                voicesOut.setSample (ch, i, on * 0.9f * std::tanh (z * (1.0f / 0.9f)));
            }
        }
        loopWrite = (loopWrite + n) & loopMask;

        float* vo[2] = { voicesOut.getWritePointer (0), voicesOut.getWritePointer (1) };
        speedVoices.process (vo, numChannels, n);   // BUZZ
    }

    //==========================================================================
    // PATTERN

    void resetPattern() noexcept
    {
        passPos = 0.0f;
        bounceUp = false;
        scatterAt = 0.0f;
        reversePhase = 0.0f;
        patternSemis = 0.0f;
        patternStarted = false;
        scatterPrimed = false;
        timeJitter = 1.0f;
        trailPanTarget = { 1.0f, 0.72f };
    }

    /** Called once per control block: detects repeat boundaries and picks the next interval. */
    void advancePattern (int n, float loopD) noexcept
    {
        const float pitch = settings.snap ? std::round (settings.pitchSemis) : settings.pitchSemis;
        switch (activePattern)
        {
            case bounce:
            case scatter:
                passPos += (float) n;
                if (! patternStarted || passPos >= loopD)
                {
                    const bool wasStarted = patternStarted;
                    passPos = patternStarted ? passPos - loopD : 0.0f;
                    patternStarted = true;
                    if (activePattern == bounce)
                    {
                        // The DRONE (+PITCH) arrives first: its repeat goes back to the note (-PITCH),
                        // the next one up again (+PITCH), and so on.
                        if (wasStarted)
                            bounceUp = ! bounceUp;
                        patternSemis = bounceUp ? -pitch : pitch;
                        trailPanTarget = bounceUp ? std::array<float, 2> { 1.0f, 0.2f } : std::array<float, 2> { 0.2f, 1.0f };   // ping-pong
                    }
                    else
                    {
                        if (! scatterPrimed) { scatterAt = pitch; scatterPrimed = true; }   // the DRONE arrives first
                        // a random chord tone of PITCH, relative to where the repeat currently is
                        static constexpr float octaves[] { 0.0f, 0.0f, 12.0f, -12.0f };
                        const bool onVoice = rng.nextFloat() < 0.5f;
                        float next = (onVoice ? pitch : 0.0f) + octaves[(size_t) (rng.nextInt() % 4u)];
                        if (std::abs (next - scatterAt) < 0.01f)
                            next += next > 0.0f ? -12.0f : 12.0f;
                        patternSemis = sw::jlimit (-24.0f, 24.0f, next - scatterAt);
                        scatterAt = next;
                        // ...at a random place in the stereo field, a little early or late
                        const float pan = rng.nextBipolar();
                        trailPanTarget = { sw::jmin (1.0f, 1.0f - pan), sw::jmin (1.0f, 1.0f + pan) };
                        timeJitter = 0.7f + 0.6f * rng.nextFloat();
                    }
                }
                break;

            case ladder:
            case reverse:
            case bloom:
            default:
                patternSemis = pitch;
                // LADDER sits with the DRONE (slightly left); REVERSE and BLOOM spread wide
                trailPanTarget = activePattern == ladder ? std::array<float, 2> { 1.0f, 0.72f } : std::array<float, 2> { 1.0f, 1.0f };
                timeJitter = 1.0f;
                break;
        }
    }

    float readPattern (int ch, int i, float loopD) const noexcept
    {
        const float now = (float) (loopWrite + i);
        if (activePattern != reverse)
            return readLoop (ch, now - loopD);

        // REVERSE: two heads run backwards through the last window, crossfaded (sin^2)
        const float w = reverseWindow (loopD);
        float out = 0.0f;
        for (int head = 0; head < 2; ++head)
        {
            float ph = reversePhase + (head == 0 ? 0.0f : 0.5f * w);
            if (ph >= w) ph -= w;
            const float gain = std::sin (swarm::kPi * ph / w);
            out += gain * gain * readLoop (ch, now - w - 2.0f * ph);
        }
        return out;
    }

    void advanceReverse (float loopD) noexcept
    {
        if (activePattern != reverse)
            return;
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
        for (auto& ch : diffusers)
            for (auto& d : ch)
                d.clear();
        loopCleared = true;
        resetPattern();
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

    /** Schroeder all-pass for BLOOM. */
    struct AllPass
    {
        std::vector<float> buf;
        int pos = 0;
        void prepare (int length) { buf.assign ((size_t) sw::jmax (1, length), 0.0f); pos = 0; }
        void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
        float process (float x, float g) noexcept
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            pos = (pos + 1) % (int) buf.size();
            return d - g * v;
        }
    };

    //==========================================================================
    double sampleRate = 44100.0;
    Settings settings;

    ShiftRing dryRing, voiceRing, trailRing;
    PitchVoice shiftVoice, angerVoice, droneVoice, queenVoice, trailVoice;
    SpeedStage speedMain, speedVoices;
    sw::AudioBuffer<float> dry, mainOut, followIn, shifted, angerBuf, voiceIn, droneBuf, queenBuf, trailIn, trailBuf, voicesOut;
    sw::dsp::DelayLine<float, sw::dsp::DelayLineInterpolationTypes::Linear> lagLine { 1 };
    sw::SmoothedValue<float> onSmoothed, droneLevel, queenLevel, loopGain, resonance, lagSmoothed, voiceGain;
    swarm::FastRandom rng { 0x5EED1E5u };
    swarm::OnePole chaosSmoother, loopDelay;
    std::array<swarm::OnePole, 3> driftCents;
    std::array<float, 3> driftTarget {};
    int driftCounter = 0;

    // SHIFT
    float currentSemis = 0.0f, targetSemis = 0.0f, rampStep = 0.0f, displaySemis = 0.0f;
    float wet = 0.0f, angerLevel = 0.0f;
    float chaosPhase = 0.0f, chaosTarget = 0.0f, wobblePhase = 0.0f;
    float dryEnv = 0.0f, wetEnv = 0.0f, restoreGain = 1.0f;
    float dryLevel = 1.0f, dryLevelTarget = 1.0f, dryLevelCoeff = 0.001f;
    float envAttack = 0.05f, envRelease = 0.001f, gainUp = 0.02f, gainDown = 0.003f;
    std::array<float, 2> chipmunkLp {};
    std::array<swarm::SVF, 2> shiftSubsonic;
    bool shiftIdle = true, queenIdle = true, trailIdle = true;
    float lastMainRatio = 1.0f, lastAngerRatio = 1.0f, lastDroneRatio = 1.0f, lastQueenRatio = 1.0f, lastTrailRatio = 1.0f;

    // VOICES / TRAILS
    std::array<std::vector<float>, 2> loopBuf;
    int loopSize = 0, loopMask = 0, loopWrite = 0;
    bool loopCleared = false;
    float loopDelayTarget = 8000.0f, toneCoeff = 0.0f;
    std::array<float, 2> toneState {}, droneLp {}, queenLp {}, trailLp {}, loopTone {};
    std::array<swarm::SVF, 2> loopLp, loopHp, voiceSubsonic;
    std::array<swarm::DCBlocker, 2> dc;
    std::array<std::array<AllPass, kDiffusers>, 2> diffusers;

    // PATTERN
    int activePattern = ladder;
    float passPos = 0.0f, scatterAt = 0.0f, reversePhase = 0.0f, patternSemis = 0.0f;
    bool bounceUp = false, patternStarted = false, scatterPrimed = false;
    float fastEnv = 0.0f, slowEnv = 0.0f, onsetAttack = 0.3f, onsetRelease = 0.001f, onsetSlow = 0.0005f;
    int onsetHoldoff = 0;
    float timeJitter = 1.0f, wowPhase = 0.0f, flutterPhase = 0.37f;
    std::array<float, 2> trailPan { 1.0f, 0.72f }, trailPanTarget { 1.0f, 0.72f };
    float bloomEnv = 0.0f, bloomGain = 1.0f, bloomRelease = 0.0001f, bloomSwell = 0.0001f;
};
