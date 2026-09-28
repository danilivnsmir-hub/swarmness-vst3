#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

/**
 * TUNER: the audio thread only low-passes and decimates the guitar (to ~12 kHz) into a lock-free
 * ring while the tuner is open; the pitch is estimated on the UI side (YIN) from the latest window.
 * The range reaches below a low F# (8-string) and drop tunings: ~25 Hz .. 1.3 kHz.
 */
class TunerTap
{
public:
    static constexpr int kRingSize = 8192;   // ~0.68 s at 12 kHz

    void prepare (double sampleRate)
    {
        decim = juce::jmax (1, (int) std::round (sampleRate / 12000.0));
        rate.store (sampleRate / decim);
        // one-pole pair at ~2.5 kHz in front of the decimation (the fundamental is what matters)
        lpA = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 2500.0 / sampleRate));
        lp1 = lp2 = acc = 0.0f;
        count = 0;
    }

    /** Audio thread: cheap, and nothing at all while the tuner is closed. */
    void push (const float* const* audio, int numCh, int numSamples) noexcept
    {
        if (! active.load (std::memory_order_relaxed))
            return;
        auto w = writeIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            float x = audio[0][i];
            if (numCh > 1) x = 0.5f * (x + audio[1][i]);
            lp1 += lpA * (x - lp1);
            lp2 += lpA * (lp1 - lp2);
            acc += lp2;
            if (++count >= decim)
            {
                ring[(size_t) (w & (kRingSize - 1))] = acc / (float) decim;
                ++w;
                acc = 0.0f;
                count = 0;
            }
        }
        writeIndex.store (w, std::memory_order_release);
    }

    /** UI thread: the latest n samples (n <= kRingSize / 2). */
    void read (std::vector<float>& out, int n) const
    {
        out.resize ((size_t) n);
        const auto w = writeIndex.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = ring[(size_t) ((w - (unsigned) n + (unsigned) i) & (kRingSize - 1))];
    }

    double getRate() const noexcept { return rate.load(); }

    std::atomic<bool> active { false }, mute { false };
    std::atomic<float> a4 { 440.0f };   // the reference pitch (saved with the session)

private:
    std::array<float, kRingSize> ring {};
    std::atomic<unsigned> writeIndex { 0 };
    std::atomic<double> rate { 12000.0 };
    int decim = 4, count = 0;
    float lpA = 0.3f, lp1 = 0.0f, lp2 = 0.0f, acc = 0.0f;
};

/** YIN pitch estimate (UI thread). */
struct TunerEstimate
{
    float hz = 0.0f;        // 0 = no clear pitch
    float clarity = 0.0f;   // 1 - YIN's aperiodicity at the chosen lag
    float levelDb = -120.0f;

    static TunerEstimate estimate (const std::vector<float>& x, double rate)
    {
        TunerEstimate r;
        const int n = (int) x.size();
        if (n < 256) return r;
        double mean = 0.0, rms = 0.0;
        for (float v : x) mean += v;
        mean /= n;
        for (float v : x) rms += (v - mean) * (v - mean);
        rms = std::sqrt (rms / n);
        r.levelDb = (float) juce::Decibels::gainToDecibels (rms, -120.0);
        if (r.levelDb < -62.0f)
            return r;

        const int tauMin = juce::jmax (2, (int) (rate / 1300.0)), tauMax = juce::jmin (n / 2, (int) (rate / 25.0));
        const int w = n - tauMax;
        std::vector<double> d ((size_t) tauMax + 2, 0.0);
        for (int tau = 1; tau <= tauMax + 1 && tau < n - w; ++tau)
        {
            double s = 0.0;
            for (int i = 0; i < w; ++i)
            {
                const double diff = (double) x[(size_t) i] - x[(size_t) (i + tau)];
                s += diff * diff;
            }
            d[(size_t) tau] = s;
        }
        // cumulative mean normalised difference
        std::vector<double> cmnd ((size_t) tauMax + 2, 1.0);
        double running = 0.0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
        }
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (cmnd[(size_t) tau] < 0.15)
            {
                while (tau + 1 <= tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
            return r;
        // parabolic interpolation around the minimum
        const double b = cmnd[(size_t) best];
        // the minimum's position from the raw difference (unbiased by the normalisation)
        const double a = d[(size_t) (best - 1)], c = d[(size_t) (best + 1)], m = d[(size_t) best];
        const double den = a - 2.0 * m + c;
        const double shift = std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0;
        double period = best + juce::jlimit (-1.0, 1.0, shift);
        // refine at a multiple of the period (the interpolation error shrinks k times)
        const int k = (int) ((tauMax - 2) / period);
        if (k >= 2)
        {
            int t0 = (int) std::round (k * period);
            for (int t = t0 - 2; t <= t0 + 2; ++t)
                if (t >= 1 && t <= tauMax && d[(size_t) t] < d[(size_t) t0]) t0 = t;
            if (t0 > 1 && t0 < tauMax)
            {
                const double a2 = d[(size_t) (t0 - 1)], m2 = d[(size_t) t0], c2 = d[(size_t) (t0 + 1)];
                const double den2 = a2 - 2.0 * m2 + c2;
                const double s2 = std::abs (den2) > 1.0e-12 ? 0.5 * (a2 - c2) / den2 : 0.0;
                const double refined = (t0 + juce::jlimit (-1.0, 1.0, s2)) / k;
                if (std::abs (refined - period) < 0.5)
                    period = refined;
            }
        }
        r.hz = (float) (rate / period);
        r.clarity = (float) juce::jlimit (0.0, 1.0, 1.0 - b);
        return r;
    }
};
