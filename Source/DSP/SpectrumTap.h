#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

/**
 * Lock-free single-producer / single-consumer tap of the output (mono sum) for the EQ
 * analyser. The audio thread only pushes while the editor has the analyser on screen.
 */
class SpectrumTap
{
public:
    SpectrumTap() : fifo (kCapacity), data ((size_t) kCapacity, 0.0f) {}

    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive, std::memory_order_relaxed); }
    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

    /** Audio thread. */
    void push (const float* left, const float* right, int numSamples) noexcept
    {
        if (! isActive())
            return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (numSamples, s1, n1, s2, n2);
        auto write = [&] (int start, int count, int offset)
        {
            for (int i = 0; i < count; ++i)
                data[(size_t) (start + i)] = right != nullptr ? 0.5f * (left[offset + i] + right[offset + i]) : left[offset + i];
        };
        write (s1, n1, 0);
        write (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);
    }

    /** Editor: pulls up to maxSamples into dest, returns how many. */
    int pull (float* dest, int maxSamples) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (juce::jmin (maxSamples, fifo.getNumReady()), s1, n1, s2, n2);
        std::copy_n (data.begin() + s1, n1, dest);
        std::copy_n (data.begin() + s2, n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    static constexpr int kCapacity = 32768;
    juce::AbstractFifo fifo;
    std::vector<float> data;
    std::atomic<bool> active { false };
};
