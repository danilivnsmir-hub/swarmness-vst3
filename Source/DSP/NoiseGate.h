#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <vector>

/**
 * The noise gate of AMP and WASP: keyed from the guitar, it computes a gain curve that the block
 * applies to its input AND its output (like a rack gate in "X" mode), so what the amp does after a
 * note - the hiss, the thump of its stages settling - is gated too, not only the guitar's noise.
 *
 * The key is an averaged envelope (3 ms up, 25 ms down), not the peaks: a guitar's noise floor has
 * peaks some 10 dB over its level, which kept a peak-keyed gate open. GATE 0 = off, then the
 * threshold runs from -75 dBFS to -20 dBFS (50 % = -47.5); 6 dB of hysteresis, 50 ms hold, a smooth release.
 */
class NoiseGate
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        curve.assign ((size_t) juce::jmax (1, maxBlockSize), 1.0f);
        envUp = 1.0f - std::exp (-1.0f / (0.003f * (float) fs));
        envDown = 1.0f - std::exp (-1.0f / (0.025f * (float) fs));
        attack = 1.0f - std::exp (-1.0f / (0.0008f * (float) fs));
        release = 1.0f - std::exp (-1.0f / (0.060f * (float) fs));
        holdSamples = (int) (0.05 * fs);
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        gain = 1.0f;
        hold = 0;
        open = true;
    }

    /** GATE knob (0..1) -> threshold in dBFS (0 = off). */
    static float thresholdDb (float knob) noexcept { return -75.0f + 55.0f * juce::jlimit (0.0f, 1.0f, knob); }

    /** Computes the gain curve for this block from the key (the block's input, before it is touched).
        Returns nullptr when the gate is off and fully open (nothing to apply). */
    const float* compute (const float* const* key, int numCh, int numSamples, float knob) noexcept
    {
        const int n = juce::jmin (numSamples, (int) curve.size());
        if (knob <= 0.001f && gain >= 0.9999f)
        {
            gain = 1.0f;
            open = true;
            return nullptr;
        }
        const float thOpen = juce::Decibels::decibelsToGain (thresholdDb (knob)), thClose = thOpen * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f;
            for (int c = 0; c < numCh; ++c)
                a = juce::jmax (a, std::abs (key[c][i]));
            env += (a > env ? envUp : envDown) * (a - env);
            if (knob <= 0.001f || env > thOpen)
            {
                open = true;
                hold = holdSamples;
            }
            else if (open && env > thClose)
                hold = holdSamples;
            else if (hold > 0)
                --hold;
            else
                open = false;
            const float target = open ? 1.0f : 0.0f;
            gain += (target > gain ? attack : release) * (target - gain);
            curve[(size_t) i] = gain;
        }
        return curve.data();
    }

    bool isOpen() const noexcept { return open; }

private:
    double fs = 44100.0;
    std::vector<float> curve;
    float env = 0.0f, gain = 1.0f;
    float envUp = 0.1f, envDown = 0.001f, attack = 0.03f, release = 0.0003f;
    int hold = 0, holdSamples = 2205;
    bool open = true;
};
