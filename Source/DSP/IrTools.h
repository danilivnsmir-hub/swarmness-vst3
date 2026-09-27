#pragma once

#include <JuceHeader.h>
#include <cmath>

/**
 * Loaded impulse responses are checked and cleaned before they reach a convolution: files in the
 * wild carry NaN / inf samples (broken float WAVs), DC offsets, long silence, or are nearly empty.
 * Any of those, normalised blindly, turns a convolution into a hiss generator.
 */
namespace irtools
{
    /** Cleans `ir` in place. Returns an error for a file that holds no usable response. */
    inline juce::String sanitise (juce::AudioBuffer<float>& ir, double sampleRate)
    {
        const int channels = ir.getNumChannels(), length = ir.getNumSamples();
        if (channels == 0 || length < 16)
            return "the file is too short";

        int broken = 0;
        for (int c = 0; c < channels; ++c)
        {
            auto* x = ir.getWritePointer (c);
            for (int i = 0; i < length; ++i)
                if (! std::isfinite (x[i]))
                {
                    x[i] = 0.0f;
                    ++broken;
                }
        }
        if (broken > length / 100)
            return "the file is damaged (" + juce::String (broken) + " invalid samples)";

        // DC offset: a gentle 5 Hz high-pass
        const float k = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) sampleRate);
        for (int c = 0; c < channels; ++c)
        {
            auto* x = ir.getWritePointer (c);
            float lp = 0.0f;
            for (int i = 0; i < length; ++i)
            {
                lp += k * (x[i] - lp);
                x[i] -= lp;
            }
        }

        float peak = 0.0f;
        for (int c = 0; c < channels; ++c)
            peak = juce::jmax (peak, ir.getMagnitude (c, 0, length));
        if (peak < 1.0e-6f)
            return "the file is silent";

        // leading silence (keep 1 ms before the first sound) and the tail under -90 dB
        int first = length, last = 0;
        for (int c = 0; c < channels; ++c)
        {
            const auto* x = ir.getReadPointer (c);
            for (int i = 0; i < length; ++i)
                if (std::abs (x[i]) > peak * 1.0e-3f) { first = juce::jmin (first, i); break; }
            for (int i = length - 1; i >= 0; --i)
                if (std::abs (x[i]) > peak * 3.0e-5f) { last = juce::jmax (last, i); break; }
        }
        first = juce::jmax (0, first - (int) (0.001 * sampleRate));
        const int newLength = juce::jmax (16, last + 1 - first);
        juce::AudioBuffer<float> trimmed (channels, newLength);
        for (int c = 0; c < channels; ++c)
            trimmed.copyFrom (c, 0, ir, c, first, juce::jmin (newLength, length - first));
        // short fade at the very end
        const int fade = juce::jmin (newLength / 4, (int) (0.01 * sampleRate));
        for (int c = 0; c < channels; ++c)
            trimmed.applyGainRamp (c, newLength - fade, fade, 1.0f, 0.0f);
        ir = std::move (trimmed);
        return {};
    }

    /** Sum of squares over all channels (the response's energy). */
    inline double energy (const juce::AudioBuffer<float>& ir)
    {
        double e = 0.0;
        for (int c = 0; c < ir.getNumChannels(); ++c)
        {
            const auto* x = ir.getReadPointer (c);
            for (int i = 0; i < ir.getNumSamples(); ++i)
                e += (double) x[i] * x[i];
        }
        return e;
    }
}
