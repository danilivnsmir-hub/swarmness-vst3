#pragma once

#include "DSPUtils.h"
#include <atomic>

/**
 * HONEY: a pedal-style sustainer / compressor - sticky, even sustain in front of the amp (or
 * anywhere in the chain), the way a compressor pedal makes a clean part shine.
 *
 *  SUSTAIN : threshold down and ratio up together, like one knob on a pedal:
 *            0 = 1.5:1 above -20 dBFS (just a touch), 100 = 10:1 above -45 dBFS (everything squashed)
 *  ATTACK  : 1 .. 40 ms - slower lets the pick through before the squash
 *  BLEND   : parallel blend, dry -> compressed (100 = compressed only)
 *  LEVEL   : +-12 dB on top of the automatic make-up (SUSTAIN alone keeps the loudness about even)
 *  LIMIT   : a fast peak limiter after the compressor (0.2 ms attack, 60 ms release, ceiling -6 dBFS -
 *            the top of the IN meter's green zone): the picks that get through a slow ATTACK are
 *            stopped before the amp
 *
 * Detector: the side-chain is high-passed at 120 Hz (the low strings must not pump everything),
 * a 3 ms RMS / peak blend, linked across channels. The release is programme-dependent, like an
 * optical compressor: fast after a short hit, slow (up to 400 ms) the deeper the gain reduction.
 * No latency; off = bit-transparent.
 */
class HoneyBlock
{
public:
    struct Settings
    {
        bool on = false;
        float sustain = 0.5f, attack = 0.5f, blend = 1.0f;   // 0..1
        float levelDb = 0.0f;
        bool limit = false;
    };

    void prepare (double sr, int /*maxBlockSize*/)
    {
        fs = sr;
        for (auto& f : sideHp)
        {
            f.setType (swarm::SVF::Type::highPass);
            f.setParams (sr, 120.0f, 0.7071f);
        }
        rmsCoeff = (float) (1.0 - std::exp (-1.0 / (0.003 * sr)));
        limAttack = (float) (1.0 - std::exp (-1.0 / (0.0002 * sr)));
        limRelease = (float) (1.0 - std::exp (-1.0 / (0.06 * sr)));
        peakDecay = (float) std::exp (-1.0 / (0.002 * sr));
        onGain.reset (sr, 0.01);
        onGain.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        levelGain.reset (sr, 0.02);
        blendSmoothed.reset (sr, 0.02);
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : sideHp) f.reset();
        rms2 = peak = 0.0f;
        grDb = limGr = 0.0f;
        meterGr.store (0.0f);
    }

    void setParams (const Settings& s) noexcept
    {
        settings = s;
        settings.sustain = sw::jlimit (0.0f, 1.0f, s.sustain);
        settings.attack = sw::jlimit (0.0f, 1.0f, s.attack);
        settings.blend = sw::jlimit (0.0f, 1.0f, s.blend);
        onGain.setTargetValue (settings.on ? 1.0f : 0.0f);
        levelGain.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb + makeupDb (settings.sustain)));
        blendSmoothed.setTargetValue (settings.blend);
        // ATTACK: 1 .. 40 ms, exponential in the knob
        const double attackSeconds = 0.001 * std::pow (40.0, (double) settings.attack);
        attackCoeff = (float) (1.0 - std::exp (-1.0 / (attackSeconds * fs)));
    }

    bool isIdle() const noexcept { return ! settings.on && ! onGain.isSmoothing() && onGain.getCurrentValue() <= 0.0f; }

    static constexpr float kLimitCeilingDb = -6.0f;

    /** Gain reduction of the last block, dB (for the editor): the compressor's plus the limiter's. */
    float gainReductionDb() const noexcept { return meterGr.load (std::memory_order_relaxed); }

    //==========================================================================
    /** The static curve (dB in -> dB out) for SUSTAIN s - also used by the tests. */
    static float thresholdDb (float s) noexcept { return -20.0f - 25.0f * s; }
    static float ratio (float s) noexcept { return 1.5f + 8.5f * s * s; }
    /** Make-up: half of what the curve takes off a -20 dBFS signal (0 .. +11 dB) - the loudness stays about
        even, and a pick that slips through a slow attack has headroom left. */
    static float makeupDb (float s) noexcept { return 0.5f * (-thresholdDb (s) - 20.0f) * (1.0f - 1.0f / ratio (s)); }
    static float gainReductionFor (float levelDb, float s) noexcept
    {
        constexpr float knee = 6.0f;
        const float t = thresholdDb (s), r = ratio (s), over = levelDb - t;
        if (over <= -0.5f * knee) return 0.0f;
        if (over >= 0.5f * knee)  return over - over / r;
        const float x = over + 0.5f * knee;
        return (1.0f - 1.0f / r) * x * x / (2.0f * knee);
    }
    /** What the detector reads for a steady sine of amplitude a (the tests predict levels with it). */
    static float detectorOfSine (float a) noexcept { return 0.6f * a * 0.70710678f + 0.4f * a; }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        if (isIdle())
        {
            meterGr.store (0.0f, std::memory_order_relaxed);
            return;
        }
        numChannels = sw::jmin (numChannels, 2);
        const float s = settings.sustain;
        float blockGr = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- side-chain: high-passed, linked, RMS / peak blend
            float sc = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sc = sw::jmax (sc, std::abs (sideHp[(size_t) ch].process (audio[ch][i])));
            rms2 += rmsCoeff * (sc * sc - rms2);
            peak = sw::jmax (sc, peak * peakDecay);
            const float det = 0.6f * std::sqrt (rms2) + 0.4f * peak;
            const float detDb = 20.0f * std::log10 (det + 1.0e-7f);

            // ---- gain computer and ballistics (dB domain)
            const float target = gainReductionFor (detDb, s);
            if (target > grDb)
                grDb += attackCoeff * (target - grDb);
            else
            {
                // optical-style release: slow when the reduction is deep, quick after a short hit
                const float releaseSeconds = 0.08f + 0.32f * sw::jmin (1.0f, grDb / 12.0f);
                grDb += (float) (1.0 / (releaseSeconds * fs)) * (target - grDb);
            }
            blockGr = sw::jmax (blockGr, grDb);

            const float gain = levelGain.getNextValue() * juce::Decibels::decibelsToGain (-grDb);
            const float blend = blendSmoothed.getNextValue();
            const float on = onGain.getNextValue();
            float wet[2] {};
            float pk = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = audio[ch][i];
                wet[ch] = dry + blend * (dry * gain - dry);
                pk = sw::jmax (pk, std::abs (wet[ch]));
            }
            // LIMIT: a fast peak limiter at the ceiling (linked), released slowly
            {
                const float over = settings.limit && pk > kLimitCeiling ? 20.0f * std::log10 (pk / kLimitCeiling) : 0.0f;
                limGr += (over > limGr ? limAttack : limRelease) * (over - limGr);
                if (limGr > 0.01f)
                {
                    const float lg = juce::Decibels::decibelsToGain (-limGr);
                    for (int ch = 0; ch < numChannels; ++ch)
                        wet[ch] *= lg;
                    blockGr = sw::jmax (blockGr, grDb + limGr);
                }
            }
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = audio[ch][i];
                float w = wet[ch];
                // a soft ceiling, only above -1 dBFS
                if (w > 0.9f)       w = 0.9f + 0.1f * std::tanh ((w - 0.9f) * 10.0f);
                else if (w < -0.9f) w = -0.9f + 0.1f * std::tanh ((w + 0.9f) * 10.0f);
                audio[ch][i] = dry + on * (w - dry);
            }
        }
        meterGr.store (blockGr, std::memory_order_relaxed);
    }

private:
    double fs = 48000.0;
    Settings settings;
    std::array<swarm::SVF, 2> sideHp;
    static constexpr float kLimitCeiling = 0.501187f;   // -6 dBFS
    float rmsCoeff = 0.004f, peakDecay = 0.99f, attackCoeff = 0.02f, limAttack = 0.1f, limRelease = 0.0003f;
    float rms2 = 0.0f, peak = 0.0f, grDb = 0.0f, limGr = 0.0f;
    juce::SmoothedValue<float> onGain, levelGain, blendSmoothed;
    std::atomic<float> meterGr { 0.0f };
};
