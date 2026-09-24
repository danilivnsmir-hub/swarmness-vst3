#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <complex>
#include <vector>

/**
 * High-fidelity spectral pitch shifter ("STUDIO" quality).
 *
 * Phase vocoder with peak-locked region shifting (Laroche & Dolson, 1999):
 *  - every spectral peak is located precisely from its instantaneous frequency,
 *  - the region of bins around each peak is moved as one block (keeps the partial's
 *    shape and phase relationships, i.e. no "phasiness"),
 *  - the region's phase is advanced by (ratio - 1) * omega * hop per frame, so every
 *    transposed partial lands exactly on the target frequency.
 *
 * 4096-point FFT (8192 above 50 kHz) with 87.5% overlap. At ratio 1 the output is a
 * perfect reconstruction of the input. Fixed latency, reported to the host.
 */
class StudioPitchShifter
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
    {
        const int order = sampleRate <= 50000.0 ? 12 : 13;
        fftSize = 1 << order;
        hop = fftSize / 8;
        latency = fftSize - hop;
        numBins = fftSize / 2 + 1;

        fft = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

        // Sum of hann^2 at 1/8 hop is exactly 3.
        outputGain = 1.0f / 3.0f;

        channels.clear();
        channels.resize ((size_t) numChannels);
        for (auto& c : channels)
        {
            c.inFifo .assign ((size_t) fftSize, 0.0f);
            c.outFifo.assign ((size_t) fftSize, 0.0f);
            c.accum  .assign ((size_t) fftSize * 2, 0.0f);
            c.prevPhase.assign ((size_t) numBins, 0.0f);
            c.prevRot  .assign ((size_t) numBins, 0.0f);
            c.newRot   .assign ((size_t) numBins, 0.0f);
        }

        fftData.assign ((size_t) fftSize * 2, 0.0f);
        spectrum.assign ((size_t) numBins, {});
        shifted .assign ((size_t) numBins, {});
        magnitude.assign ((size_t) numBins, 0.0f);
        trueBin  .assign ((size_t) numBins, 0.0f);
        peaks.reserve ((size_t) numBins);

        reset();
    }

    void reset()
    {
        for (auto& c : channels)
        {
            std::fill (c.inFifo.begin(),  c.inFifo.end(),  0.0f);
            std::fill (c.outFifo.begin(), c.outFifo.end(), 0.0f);
            std::fill (c.accum.begin(),   c.accum.end(),   0.0f);
            std::fill (c.prevPhase.begin(), c.prevPhase.end(), 0.0f);
            std::fill (c.prevRot.begin(),   c.prevRot.end(),   0.0f);
        }
        rover = latency;
    }

    int getLatencySamples() const noexcept { return fftSize; }   // fifo offset + one hop of overlap-add

    /** Processes numSamples in place; the ratio is applied from the next analysis frame on. */
    void process (float* const* audio, int numChannels, int numSamples, float newRatio) noexcept
    {
        ratio = newRatio;
        numChannels = juce::jmin (numChannels, (int) channels.size());

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& c = channels[(size_t) ch];
                c.inFifo[(size_t) rover] = audio[ch][i];
                audio[ch][i] = c.outFifo[(size_t) (rover - latency)];
            }

            if (++rover >= fftSize)
            {
                rover = latency;
                for (int ch = 0; ch < numChannels; ++ch)
                    processFrame (channels[(size_t) ch]);
            }
        }
    }

private:
    struct Channel
    {
        std::vector<float> inFifo, outFifo, accum;
        std::vector<float> prevPhase, prevRot, newRot;
    };

    static float wrapPhase (float p) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        return p - twoPi * std::floor ((p + juce::MathConstants<float>::pi) / twoPi);
    }

    void processFrame (Channel& c) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // --- Analysis
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = c.inFifo[(size_t) i] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fftData.data(), true);

        const float binsPerRadian = (float) fftSize / (twoPi * (float) hop);
        float maxMag = 0.0f;

        for (int k = 0; k < numBins; ++k)
        {
            const std::complex<float> x (fftData[(size_t) (2 * k)], fftData[(size_t) (2 * k + 1)]);
            spectrum[(size_t) k] = x;

            const float mag = std::abs (x);
            magnitude[(size_t) k] = mag;
            maxMag = juce::jmax (maxMag, mag);

            const float phase = std::arg (x);
            const float expected = twoPi * (float) k * (float) hop / (float) fftSize;
            const float deviation = wrapPhase (phase - c.prevPhase[(size_t) k] - expected);
            c.prevPhase[(size_t) k] = phase;
            trueBin[(size_t) k] = (float) k + deviation * binsPerRadian;
        }

        std::fill (shifted.begin(), shifted.end(), std::complex<float> {});

        // --- Peak picking
        peaks.clear();
        const float threshold = juce::jmax (1.0e-9f, maxMag * 1.0e-5f);   // -100 dB relative
        for (int k = 1; k < numBins - 1; ++k)
        {
            const float m = magnitude[(size_t) k];
            if (m > threshold && m > magnitude[(size_t) (k - 1)] && m >= magnitude[(size_t) (k + 1)]
                && (k < 2 || m > magnitude[(size_t) (k - 2)]) && (k >= numBins - 2 || m >= magnitude[(size_t) (k + 2)]))
                peaks.push_back (k);
        }

        if (! peaks.empty() && maxMag > 1.0e-9f)
        {
            const float r = ratio;
            const float hopF = (float) hop;
            int regionStart = 0;

            for (size_t p = 0; p < peaks.size(); ++p)
            {
                const int peak = peaks[p];

                // Region ends at the magnitude minimum between this peak and the next one.
                int regionEnd = numBins - 1;
                if (p + 1 < peaks.size())
                {
                    const int next = peaks[p + 1];
                    regionEnd = peak;
                    for (int k = peak + 1; k < next; ++k)
                        if (magnitude[(size_t) k] < magnitude[(size_t) regionEnd])
                            regionEnd = k;
                }

                const float peakBin = trueBin[(size_t) peak];
                const int shift = (int) std::lround ((r - 1.0f) * peakBin);
                const float omega = twoPi * peakBin / (float) fftSize;
                const float theta = wrapPhase (c.prevRot[(size_t) peak] + (r - 1.0f) * omega * hopF);
                const std::complex<float> rot (std::cos (theta), std::sin (theta));

                for (int k = regionStart; k <= regionEnd; ++k)
                {
                    c.newRot[(size_t) k] = theta;
                    const int target = k + shift;
                    if (target >= 0 && target < numBins)
                        shifted[(size_t) target] += spectrum[(size_t) k] * rot;
                }

                regionStart = regionEnd + 1;
            }

            std::swap (c.prevRot, c.newRot);
        }
        else
        {
            std::fill (c.prevRot.begin(), c.prevRot.end(), 0.0f);
        }

        // --- Synthesis
        for (int k = 0; k < numBins; ++k)
        {
            fftData[(size_t) (2 * k)]     = shifted[(size_t) k].real();
            fftData[(size_t) (2 * k + 1)] = shifted[(size_t) k].imag();
        }
        fftData[1] = 0.0f;                               // DC is real
        fftData[(size_t) (2 * (numBins - 1) + 1)] = 0.0f; // Nyquist is real
        std::fill (fftData.begin() + 2 * numBins, fftData.end(), 0.0f);
        fft->performRealOnlyInverseTransform (fftData.data());

        for (int i = 0; i < fftSize; ++i)
            c.accum[(size_t) i] += fftData[(size_t) i] * window[(size_t) i] * outputGain;

        for (int i = 0; i < hop; ++i)
            c.outFifo[(size_t) i] = c.accum[(size_t) i];

        std::copy (c.accum.begin() + hop, c.accum.begin() + hop + fftSize, c.accum.begin());
        std::copy (c.inFifo.begin() + hop, c.inFifo.end(), c.inFifo.begin());
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 4096, hop = 512, latency = 3584, numBins = 2049;   // 'latency' = input FIFO offset
    int rover = 0;
    float ratio = 1.0f, outputGain = 1.0f / 3.0f;

    std::vector<float> window, fftData, magnitude, trueBin;
    std::vector<std::complex<float>> spectrum, shifted;
    std::vector<int> peaks;
    std::vector<Channel> channels;
};
