#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

namespace swarm
{
    constexpr float kPi    = juce::MathConstants<float>::pi;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    /** 4-point, 3rd-order Hermite interpolation (x is the fractional offset between y1 and y2). */
    inline float hermite (float y0, float y1, float y2, float y3, float x) noexcept
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * x + c2) * x + c1) * x + y1;
    }

    /** Small, fast, allocation-free PRNG (xorshift32) that is safe for the audio thread. */
    class FastRandom
    {
    public:
        explicit FastRandom (uint32_t seed = 0x9E3779B9u) noexcept : state (seed != 0 ? seed : 1u) {}

        void seed (uint32_t s) noexcept { state = s != 0 ? s : 1u; }

        uint32_t nextInt() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        /** Uniform in [0, 1). */
        float nextFloat() noexcept { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }

        /** Uniform in [-1, 1). */
        float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    private:
        uint32_t state;
    };

    /** One-pole smoother with a time constant, cheap enough to run per sample. */
    class OnePole
    {
    public:
        void setTime (double sampleRate, double seconds) noexcept
        {
            coeff = seconds <= 0.0 ? 1.0f : (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
        }

        void reset (float v) noexcept { z = v; }
        float process (float target) noexcept { z += coeff * (target - z); return z; }

        /** Equivalent to calling process (target) numSamples times. */
        float advance (float target, int numSamples) noexcept
        {
            z += (1.0f - std::pow (1.0f - coeff, (float) numSamples)) * (target - z);
            return z;
        }
        float get() const noexcept { return z; }

    private:
        float coeff = 1.0f, z = 0.0f;
    };

    /**
     * Zero-delay-feedback state variable filter (Cytomic / Andrew Simper topology).
     * Stable under fast modulation, which makes it ideal for smoothly automated EQ.
     */
    class SVF
    {
    public:
        enum class Type { lowPass, highPass, bell };

        void setType (Type t) noexcept { type = t; }

        /** Recomputes coefficients. gainDb only matters for the bell type. */
        void setParams (double sampleRate, float freq, float q, float gainDb = 0.0f) noexcept
        {
            freq = juce::jlimit (5.0f, (float) (sampleRate * 0.49), freq);
            const float g = std::tan (kPi * freq / (float) sampleRate);

            if (type == Type::bell)
            {
                const float A = std::pow (10.0f, gainDb / 40.0f);
                k  = 1.0f / (q * A);
                m1 = k * (A * A - 1.0f);
            }
            else
            {
                k = 1.0f / q;
            }

            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        float process (float v0) noexcept
        {
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;

            switch (type)
            {
                case Type::lowPass:  return v2;
                case Type::highPass: return v0 - k * v1 - v2;
                case Type::bell:     return v0 + m1 * v1;
            }
            return v0;
        }

    private:
        Type type = Type::lowPass;
        float k = 1.41421356f, m1 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    /** First-order DC blocker (~5 Hz). */
    class DCBlocker
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            r = (float) std::exp (-kTwoPi * 5.0 / sampleRate);
            reset();
        }

        void reset() noexcept { x1 = y1 = 0.0f; }

        float process (float x) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x;
            y1 = y;
            return y;
        }

    private:
        float r = 0.9995f, x1 = 0.0f, y1 = 0.0f;
    };

    /** Equal-power crossfade gains for a mix amount in [0, 1]. */
    inline void equalPowerGains (float mix, float& dryGain, float& wetGain) noexcept
    {
        dryGain = std::cos (mix * kPi * 0.5f);
        wetGain = std::sin (mix * kPi * 0.5f);
    }
}
