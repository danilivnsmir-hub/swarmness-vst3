#pragma once

#include "DSPUtils.h"

/**
 * Generates the time-varying pitch offset (in semitones) that sits on top of the
 * static octave/semitone shift. Runs at control rate (called once per sub-block).
 *
 *  - Random  : smooth S-curve glides between random targets within +/- range.
 *  - Rush    : organic, slowly wandering drift made of two de-correlated noise sources.
 *  - Anger   : rhythmic, semitone-quantised jumps whose probability rises with the amount.
 */
class PitchModulator
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        angerSmoother.setTime (sampleRate, 0.004);
        reset();
    }

    void reset()
    {
        randPhase = 0.0f; randFrom = randTo = 0.0f;
        rushPhaseA = rushPhaseB = 0.0f;
        rushA0 = rushA1 = rushB0 = rushB1 = 0.0f;
        angerPhase = 0.0f; angerTarget = 0.0f;
        angerSmoother.reset (0.0f);
        last = 0.0f;
    }

    void setRandom (float rangeSemitones, float speedHz) noexcept { randRange = rangeSemitones; randSpeed = speedHz; }

    /** rush / anger in [0, 1], rate in Hz. */
    void setModulation (float rushAmount, float angerAmount, float rateHz) noexcept
    {
        rush = rushAmount;
        anger = angerAmount;
        rate = rateHz;
    }

    /** Advances the generators by numSamples and returns the pitch offset in semitones. */
    float advance (int numSamples) noexcept
    {
        const float dt = (float) numSamples / (float) sampleRate;
        float result = 0.0f;

        // --- Random range: smoothstep glide between random targets
        randPhase += randSpeed * dt;
        while (randPhase >= 1.0f)
        {
            randPhase -= 1.0f;
            randFrom = randTo;
            randTo   = rng.nextBipolar();
        }
        {
            const float t = randPhase * randPhase * (3.0f - 2.0f * randPhase);
            result += (randFrom + (randTo - randFrom) * t) * randRange;
        }

        // --- Rush: two cosine-interpolated noise sources at non-harmonic rates
        if (rush > 0.0001f || std::abs (rushA1) + std::abs (rushB1) > 0.0f)
        {
            rushPhaseA += rate * dt;
            rushPhaseB += rate * 1.618f * dt;
            while (rushPhaseA >= 1.0f) { rushPhaseA -= 1.0f; rushA0 = rushA1; rushA1 = rng.nextBipolar(); }
            while (rushPhaseB >= 1.0f) { rushPhaseB -= 1.0f; rushB0 = rushB1; rushB1 = rng.nextBipolar(); }

            const float wa = 0.5f - 0.5f * std::cos (rushPhaseA * swarm::kPi);
            const float wb = 0.5f - 0.5f * std::cos (rushPhaseB * swarm::kPi);
            const float drift = 0.7f * (rushA0 + (rushA1 - rushA0) * wa)
                              + 0.3f * (rushB0 + (rushB1 - rushB0) * wb);

            const float depth = rush * rush * 12.0f;   // quadratic taper: subtle detune -> full octave
            result += drift * depth;
        }

        // --- Anger: probabilistic quantised jumps at 3x the modulation rate
        angerPhase += rate * 3.0f * dt;
        while (angerPhase >= 1.0f)
        {
            angerPhase -= 1.0f;

            if (anger > 0.0001f && rng.nextFloat() < 0.15f + 0.85f * anger)
            {
                const float maxJump = 1.0f + anger * 11.0f;
                angerTarget = std::round (rng.nextBipolar() * maxJump);
            }
            else
            {
                angerTarget = 0.0f;
            }
        }
        if (anger <= 0.0001f)
            angerTarget = 0.0f;

        result += angerSmoother.advance (angerTarget, numSamples);

        last = result;
        return result;
    }

    float getLast() const noexcept { return last; }

private:
    double sampleRate = 44100.0;
    swarm::FastRandom rng { 0x5EED1234u };

    float randRange = 0.0f, randSpeed = 1.0f;
    float rush = 0.0f, anger = 0.0f, rate = 1.0f;

    float randPhase = 0.0f, randFrom = 0.0f, randTo = 0.0f;

    float rushPhaseA = 0.0f, rushPhaseB = 0.0f;
    float rushA0 = 0.0f, rushA1 = 0.0f, rushB0 = 0.0f, rushB1 = 0.0f;

    float angerPhase = 0.0f, angerTarget = 0.0f;
    swarm::OnePole angerSmoother;

    float last = 0.0f;
};
