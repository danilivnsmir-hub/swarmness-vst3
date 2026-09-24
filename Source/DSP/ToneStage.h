#pragma once

#include "DSPUtils.h"
#include "../Parameters.h"
#include <array>

/**
 * TONE section: 24 dB/oct Butterworth low cut, 12 dB/oct high cut and a broad mid bell.
 * Cutoffs are smoothed in the log-frequency domain and coefficients are refreshed at
 * control rate, so sweeping the faders is completely zipper-free.
 */
class ToneStage
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlInterval = 16;

    void prepare (double sr)
    {
        sampleRate = sr;

        for (auto& ch : chans)
        {
            ch.hp1.setType (swarm::SVF::Type::highPass);
            ch.hp2.setType (swarm::SVF::Type::highPass);
            ch.lp .setType (swarm::SVF::Type::lowPass);
            ch.mid.setType (swarm::SVF::Type::bell);
        }

        const double controlRate = sr / kControlInterval;
        lowLog .setTime (controlRate, 0.03);
        highLog.setTime (controlRate, 0.03);
        midDb  .setTime (controlRate, 0.03);
        lpMix  .setTime (controlRate, 0.02);
        hpMix  .setTime (controlRate, 0.02);

        lowLog .reset (std::log (targetLow));
        highLog.reset (std::log (targetHigh));
        midDb  .reset (targetMid);
        lpMix  .reset (targetLpMix);
        hpMix  .reset (targetHpMix);

        reset();
        updateCoefficients();
    }

    void reset()
    {
        for (auto& ch : chans)
        {
            ch.hp1.reset(); ch.hp2.reset(); ch.lp.reset(); ch.mid.reset();
        }
        counter = 0;
    }

    void setParams (float lowCutHz, float highCutHz, float midBoostDb) noexcept
    {
        targetHpMix = ParamRanges::isLowCutOff (lowCutHz) ? 0.0f : 1.0f;
        targetLow   = juce::jmax (ParamRanges::lowCutMin, lowCutHz);
        targetLpMix = ParamRanges::isHighCutOff (highCutHz) ? 0.0f : 1.0f;
        targetHigh  = juce::jmin (highCutHz, (float) (sampleRate * 0.45));
        targetMid   = midBoostDb;
    }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, kMaxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            if (counter == 0)
                updateCoefficients();
            counter = (counter + 1) % kControlInterval;

            const float lpAmount = lpMix.get();
            const float hpAmount = hpMix.get();

            for (int c = 0; c < numChannels; ++c)
            {
                auto& ch = chans[(size_t) c];
                float x = audio[c][i];

                const float hp = ch.hp2.process (ch.hp1.process (x));
                x += hpAmount * (hp - x);

                const float lp = ch.lp.process (x);
                x += lpAmount * (lp - x);

                x = ch.mid.process (x);

                audio[c][i] = x;
            }
        }
    }

private:
    void updateCoefficients() noexcept
    {
        const float low  = std::exp (lowLog .process (std::log (targetLow)));
        const float high = std::exp (highLog.process (std::log (targetHigh)));
        const float mid  = midDb.process (targetMid);
        lpMix.process (targetLpMix);
        hpMix.process (targetHpMix);

        for (auto& ch : chans)
        {
            // Butterworth 4th order = two 2nd-order sections with Q 0.5412 and 1.3066
            ch.hp1.setParams (sampleRate, low, 0.5412f);
            ch.hp2.setParams (sampleRate, low, 1.3066f);
            ch.lp .setParams (sampleRate, high, 0.7071f);
            ch.mid.setParams (sampleRate, 850.0f, 0.65f, mid);
        }
    }

    struct Channel { swarm::SVF hp1, hp2, lp, mid; };
    std::array<Channel, kMaxChannels> chans;

    double sampleRate = 44100.0;
    float targetLow = 20.0f, targetHigh = 20000.0f, targetMid = 0.0f, targetLpMix = 0.0f, targetHpMix = 0.0f;
    swarm::OnePole lowLog, highLog, midDb, lpMix, hpMix;
    int counter = 0;
};
