#pragma once

#include "DSPUtils.h"
#include <array>

/**
 * BUZZ (internally SPEED): all-pass feedback network swept by an LFO, plus amplitude modulation at the same rate.
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
        amount = sw::jlimit (0.0f, 1.0f, s);
        mixSmoothed.setTargetValue (sw::jmin (1.0f, amount * 5.0f));
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

            for (int c = 0; c < sw::jmin (numChannels, 2); ++c)
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
    sw::SmoothedValue<float> mixSmoothed;
};

