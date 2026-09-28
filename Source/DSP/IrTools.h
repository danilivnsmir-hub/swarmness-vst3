#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

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
        trimmed.clear();
        for (int c = 0; c < channels; ++c)
            trimmed.copyFrom (c, 0, ir, c, first, juce::jmin (newLength, length - first));
        // short fade at the very end
        const int fade = juce::jmin (newLength / 4, (int) (0.01 * sampleRate));
        for (int c = 0; c < channels; ++c)
            trimmed.applyGainRamp (c, newLength - fade, fade, 1.0f, 0.0f);
        ir = std::move (trimmed);
        return {};
    }

    /** Resamples an impulse response with a Kaiser-windowed sinc (message thread: allocates).
        The pass band is flat to 95 % of the lower Nyquist (within 0.01 dB), images and aliases are
        more than 100 dB down - unlike juce::dsp::Convolution's own interpolating resampler, which
        dulls the top octave by 1-2 dB. The result is the same filter at the new rate (gain from / to). */
    inline juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double from, double to)
    {
        if (from <= 0.0 || to <= 0.0 || std::abs (from - to) < 1.0e-6)
            return in;
        const int channels = in.getNumChannels(), length = in.getNumSamples();
        const double step = from / to;                                  // input samples per output sample
        const double scale = juce::jmin (1.0, to / from) * 0.95;       // cut-off in input-rate Nyquists
        const int zeroCrossings = 48;
        const double radius = zeroCrossings / scale;                    // half-width in input samples
        const double beta = 12.0;
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < sum * 1.0e-14) break;
            }
            return sum;
        };
        const double i0Beta = bessel0 (beta);
        const int outLength = juce::jmax (1, (int) std::ceil (length / step));
        juce::AudioBuffer<float> out (channels, outLength);
        std::vector<double> taps;
        for (int n = 0; n < outLength; ++n)
        {
            const double t = n * step;
            const int k0 = juce::jmax (0, (int) std::ceil (t - radius)), k1 = juce::jmin (length - 1, (int) std::floor (t + radius));
            taps.assign ((size_t) juce::jmax (0, k1 - k0 + 1), 0.0);
            for (int k = k0; k <= k1; ++k)
            {
                const double d = t - k, r = d / radius;
                const double x = juce::MathConstants<double>::pi * scale * d;
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (x) / x;
                const double w = bessel0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / i0Beta;
                taps[(size_t) (k - k0)] = scale * sinc * w;
            }
            for (int c = 0; c < channels; ++c)
            {
                const float* x = in.getReadPointer (c);
                double acc = 0.0;
                for (int k = k0; k <= k1; ++k)
                    acc += taps[(size_t) (k - k0)] * x[k];
                out.setSample (c, n, (float) (acc * step));
            }
        }
        return out;
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
