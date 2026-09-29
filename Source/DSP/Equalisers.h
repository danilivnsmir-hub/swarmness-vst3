#pragma once

#include "DSPUtils.h"
#include <array>
#include <complex>

/**
 * EQ building blocks on the Cytomic / Simper trapezoidal SVF: stable under fast modulation,
 * so every band glides smoothly when automated. The same coefficients also give the exact
 * magnitude response for the editor's curves.
 */
namespace swarm
{
    struct EqBand
    {
        enum class Type { bell, lowShelf, highShelf, highPass, lowPass };

        struct Coeffs
        {
            float g = 0.0f, k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
            float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
        };

        static Coeffs make (Type type, double sampleRate, float freq, float q, float gainDb) noexcept
        {
            Coeffs c;
            freq = juce::jlimit (5.0f, (float) (sampleRate * 0.49), freq);
            float g = std::tan (kPi * freq / (float) sampleRate);
            const float A = std::pow (10.0f, gainDb / 40.0f);
            float k = 1.0f / q;

            switch (type)
            {
                case Type::bell:      k = 1.0f / (q * A); c.m0 = 1.0f;  c.m1 = k * (A * A - 1.0f); c.m2 = 0.0f; break;
                case Type::lowShelf:  g /= std::sqrt (A);  c.m0 = 1.0f;  c.m1 = k * (A - 1.0f);  c.m2 = A * A - 1.0f; break;
                case Type::highShelf: g *= std::sqrt (A);  c.m0 = A * A; c.m1 = k * (1.0f - A) * A; c.m2 = 1.0f - A * A; break;
                case Type::highPass:  c.m0 = 1.0f; c.m1 = -k; c.m2 = -1.0f; break;
                case Type::lowPass:   c.m0 = 0.0f; c.m1 = 0.0f; c.m2 = 1.0f; break;
            }

            c.g = g;
            c.k = k;
            c.a1 = 1.0f / (1.0f + g * (g + k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            return c;
        }

        /** Exact magnitude (dB) of the digital filter at freq. */
        static float magnitudeDb (const Coeffs& c, double sampleRate, double freq) noexcept
        {
            const double w = std::tan (juce::MathConstants<double>::pi * juce::jmin (freq, sampleRate * 0.4999) / sampleRate) / (double) c.g;
            const std::complex<double> s (0.0, w);
            const auto d = s * s + (double) c.k * s + 1.0;
            const auto h = (double) c.m0 + ((double) c.m1 * s + (double) c.m2) / d;
            return (float) (20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h))));
        }

        struct State
        {
            float ic1 = 0.0f, ic2 = 0.0f;

            void reset() noexcept { ic1 = ic2 = 0.0f; }

            float process (const Coeffs& c, float v0) noexcept
            {
                const float v3 = v0 - ic2;
                const float v1 = c.a1 * ic1 + c.a2 * v3;
                const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
            }
        };
    };

    /**
     * A fixed set of SVF bands with smoothed settings and a click-free on/off.
     * Settings glide per 16-sample chunk (frequency in octaves, gain in dB).
     */
    template <int NumBands>
    class EqChain
    {
    public:
        struct BandSettings
        {
            EqBand::Type type = EqBand::Type::bell;
            float freq = 1000.0f, q = 0.7071f, gainDb = 0.0f;
        };

        void prepare (double sr) noexcept
        {
            sampleRate = sr;
            glide = (float) (1.0 - std::exp (-(double) kChunk / (0.025 * sr)));
            onGain.reset (sr, 0.02);
            onGain.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
            outGain.reset (sr, 0.02);
            outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelDb));
            current = target;
            updateAll();
            reset();
        }

        void reset() noexcept
        {
            for (auto& ch : states)
                for (auto& s : ch)
                    s.reset();
        }

        void setOn (bool shouldBeOn) noexcept
        {
            on = shouldBeOn;
            onGain.setTargetValue (on ? 1.0f : 0.0f);
        }

        void setBand (int index, const BandSettings& s) noexcept { target[(size_t) index] = s; }

        void setLevelDb (float db) noexcept
        {
            levelDb = db;
            outGain.setTargetValue (juce::Decibels::decibelsToGain (db));
        }

        bool isIdle() const noexcept { return ! on && ! onGain.isSmoothing() && onGain.getCurrentValue() <= 0.0f; }

        const BandSettings& getBandTarget (int index) const noexcept { return target[(size_t) index]; }

        void process (float* const* audio, int numChannels, int numSamples) noexcept
        {
            if (isIdle())
            {
                wasIdle = true;
                return;
            }
            if (wasIdle)
            {
                // Re-entering from idle: start from settled settings and clean filter states.
                current = target;
                updateAll();
                reset();
                wasIdle = false;
            }

            for (int start = 0; start < numSamples; start += kChunk)
            {
                const int n = juce::jmin (kChunk, numSamples - start);
                glideTowardsTarget();

                for (int i = start; i < start + n; ++i)
                {
                    const float mix = onGain.getNextValue();
                    const float level = outGain.getNextValue();
                    for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
                    {
                        const float dry = audio[ch][i];
                        float x = dry;
                        for (int b = 0; b < NumBands; ++b)
                            x = states[(size_t) ch][(size_t) b].process (coeffs[(size_t) b], x);
                        x *= level;
                        audio[ch][i] = dry + mix * (x - dry);
                    }
                }
            }
        }

    private:
        static constexpr int kChunk = 16;

        void glideTowardsTarget() noexcept
        {
            for (int b = 0; b < NumBands; ++b)
            {
                auto& c = current[(size_t) b];
                const auto& t = target[(size_t) b];
                c.type = t.type;
                const float lf = std::log2 (c.freq), lt = std::log2 (t.freq);
                const float lq = std::log2 (c.q), lqt = std::log2 (t.q);
                if (std::abs (lf - lt) < 1.0e-4f && std::abs (c.gainDb - t.gainDb) < 1.0e-3f && std::abs (lq - lqt) < 1.0e-4f)
                {
                    if (! settled[(size_t) b])
                    {
                        c = t;
                        update (b);
                        settled[(size_t) b] = true;
                    }
                    continue;
                }
                settled[(size_t) b] = false;
                c.freq   = std::exp2 (lf + glide * (lt - lf));
                c.q      = std::exp2 (lq + glide * (lqt - lq));
                c.gainDb += glide * (t.gainDb - c.gainDb);
                update (b);
            }
        }

        void update (int b) noexcept
        {
            const auto& c = current[(size_t) b];
            coeffs[(size_t) b] = EqBand::make (c.type, sampleRate, c.freq, c.q, c.gainDb);
        }

        void updateAll() noexcept
        {
            for (int b = 0; b < NumBands; ++b)
                update (b);
        }

        double sampleRate = 44100.0;
        float glide = 0.1f, levelDb = 0.0f;
        bool on = false, wasIdle = true;
        std::array<BandSettings, (size_t) NumBands> target {}, current {};
        std::array<EqBand::Coeffs, (size_t) NumBands> coeffs {};
        std::array<bool, (size_t) NumBands> settled {};
        std::array<std::array<EqBand::State, (size_t) NumBands>, 2> states {};
        juce::SmoothedValue<float> onGain, outGain;
    };

    //==============================================================================
    /** COMB: ten octave bands (31 Hz .. 16 kHz), +-12 dB, plus LEVEL. */
    class GraphicEq
    {
    public:
        static constexpr int numBands = 10;
        static constexpr float bandQ = 1.4f;

        void prepare (double sr) noexcept
        {
            for (int b = 0; b < numBands; ++b)
                eq.setBand (b, { EqBand::Type::bell, bandHz (b), bandQ, 0.0f });
            eq.prepare (sr);
        }
        void reset() noexcept { eq.reset(); }

        static float bandHz (int b) noexcept { return 31.25f * std::exp2 ((float) b); }

        void setParams (bool on, const std::array<float, numBands>& gainsDb, float levelDb) noexcept
        {
            eq.setOn (on);
            for (int b = 0; b < numBands; ++b)
                eq.setBand (b, { EqBand::Type::bell, bandHz (b), bandQ, gainsDb[(size_t) b] });
            eq.setLevelDb (levelDb);
        }

        void process (float* const* audio, int numChannels, int numSamples) noexcept { eq.process (audio, numChannels, numSamples); }

        /** Response of a given setting (for the editor). */
        static float responseDb (const std::array<float, numBands>& gainsDb, float levelDb, double sampleRate, double freq) noexcept
        {
            float db = levelDb;
            for (int b = 0; b < numBands; ++b)
                if (std::abs (gainsDb[(size_t) b]) > 0.01f)
                    db += EqBand::magnitudeDb (EqBand::make (EqBand::Type::bell, sampleRate, bandHz (b), bandQ, gainsDb[(size_t) b]), sampleRate, freq);
            return db;
        }

    private:
        EqChain<numBands> eq;
    };

    //==============================================================================
    /** CARVE: 24 dB/oct low + high cut, low / high shelf and three bells. */
    class ParametricEq
    {
    public:
        struct Settings
        {
            float hpHz = 10.0f, lpHz = 22000.0f;
            float lowHz = 100.0f, lowDb = 0.0f;
            std::array<float, 3> bellHz { 250.0f, 800.0f, 3000.0f }, bellDb {}, bellQ { 1.0f, 1.0f, 1.0f };
            float highHz = 6000.0f, highDb = 0.0f;
        };

        static constexpr int numFilters = 9;   // hp x2, low shelf, 3 bells, high shelf, lp x2
        static constexpr float shelfQ = 0.7071f;
        // 4th-order Butterworth = two 2nd-order sections
        static constexpr float butterQ1 = 0.5411961f, butterQ2 = 1.3065630f;

        void prepare (double sr) noexcept
        {
            sampleRate = sr;
            setParams (false, {});
            eq.prepare (sr);
        }
        void reset() noexcept { eq.reset(); }

        void setParams (bool on, const Settings& s) noexcept
        {
            eq.setOn (on);
            const auto bands = makeBands (s, juce::jmin (s.lpHz, (float) (sampleRate * 0.45)));
            for (int b = 0; b < numFilters; ++b)
                eq.setBand (b, bands[(size_t) b]);
        }

        void process (float* const* audio, int numChannels, int numSamples) noexcept { eq.process (audio, numChannels, numSamples); }

        /** Response of the whole EQ for a setting (for the editor). */
        static float responseDb (const Settings& s, double sampleRate, double freq) noexcept
        {
            const auto bands = makeBands (s, juce::jmin (s.lpHz, (float) (sampleRate * 0.45)));
            float db = 0.0f;
            for (const auto& b : bands)
                db += EqBand::magnitudeDb (EqBand::make (b.type, sampleRate, b.freq, b.q, b.gainDb), sampleRate, freq);
            return db;
        }

        /** Response of one band (0 = low cut, 1 = low shelf, 2..4 = bells, 5 = high shelf, 6 = high cut). */
        static float bandResponseDb (const Settings& s, int node, double sampleRate, double freq) noexcept
        {
            const auto bands = makeBands (s, juce::jmin (s.lpHz, (float) (sampleRate * 0.45)));
            auto one = [&] (int i) { const auto& b = bands[(size_t) i]; return EqBand::magnitudeDb (EqBand::make (b.type, sampleRate, b.freq, b.q, b.gainDb), sampleRate, freq); };
            switch (node)
            {
                case 0:  return one (0) + one (1);
                case 6:  return one (7) + one (8);
                default: return one (node + 1);
            }
        }

    private:
        using BandSettings = EqChain<numFilters>::BandSettings;

        static std::array<BandSettings, numFilters> makeBands (const Settings& s, float lpHz) noexcept
        {
            using T = EqBand::Type;
            return { BandSettings { T::highPass,  s.hpHz,  butterQ1, 0.0f },
                     BandSettings { T::highPass,  s.hpHz,  butterQ2, 0.0f },
                     BandSettings { T::lowShelf,  s.lowHz, shelfQ, s.lowDb },
                     BandSettings { T::bell,      s.bellHz[0], s.bellQ[0], s.bellDb[0] },
                     BandSettings { T::bell,      s.bellHz[1], s.bellQ[1], s.bellDb[1] },
                     BandSettings { T::bell,      s.bellHz[2], s.bellQ[2], s.bellDb[2] },
                     BandSettings { T::highShelf, s.highHz, shelfQ, s.highDb },
                     BandSettings { T::lowPass,   lpHz,     butterQ1, 0.0f },
                     BandSettings { T::lowPass,   lpHz,     butterQ2, 0.0f } };
        }

        double sampleRate = 44100.0;
        EqChain<numFilters> eq;
    };
}
