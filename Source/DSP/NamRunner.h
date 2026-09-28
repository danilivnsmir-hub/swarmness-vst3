#pragma once

#include <JuceHeader.h>
#include "../Amp/NamModel.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

/**
 * Runs a loaded NAM capture inside a host-rate block: the AMP's NAM channel and WASP's NAM mode.
 *
 * The capture runs at its own rate (usually 48 kHz) on the mono sum. The signal is oversampled
 * first (to ~176-192 kHz), where it is smooth enough for cubic resampling both ways; the
 * oversampler's decimation filter removes the resampling images.
 *
 * Levels: captures that carry their reamp level (input_level_dbu) are fed at that level (as the
 * NAM plug-in does); amp captures can also be normalised to a common loudness. A pedal capture
 * keeps its own gain - that gain is the pedal.
 *
 * Threads: setModel / setSize / releaseRetired on the message (or housekeeping) thread, the rest
 * on the audio thread. The swap is wait-free for the audio thread (a try-lock).
 */
class NamRunner
{
public:
    /** 0 dBFS at the plug-in's input in dBu (a sine at full scale: 2.5 V peak). */
    static constexpr double kInputDbu = 7.17;

    explicit NamRunner (bool normaliseLoudnessTo18dB) : normaliseLoudness (normaliseLoudnessTo18dB) {}

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        osLog2 = sampleRate < 60000.0 ? 2 : (sampleRate < 120000.0 ? 1 : 0);
        fsOs = fs * (1 << osLog2);
        oversampler.reset();
        if (osLog2 > 0)
        {
            oversampler = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) 2, (size_t) osLog2,
                                                                            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversampler->initProcessing ((size_t) maxBlock);
        }
        // any capture rate up to 192 kHz
        modelIn.assign ((size_t) (maxModelBlock (192000.0) + 16), 0.0f);
        modelOut.assign (modelIn.size(), 0.0f);
        outQueue.assign ((size_t) (maxBlock * (1 << osLog2) * 2 + 1024), 0.0f);
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            if (active != nullptr) active->reset (maxModelBlock (active->getSampleRate()));
            if (pending != nullptr) pending->reset (maxModelBlock (pending->getSampleRate()));
        }
        reset();
    }

    void reset() noexcept
    {
        if (oversampler != nullptr) oversampler->reset();
        resetStream();
    }

    /** Hands over a capture (message thread; nullptr clears). The old one is freed later. */
    void setModel (std::unique_ptr<NamModel> model)
    {
        if (model != nullptr)
        {
            model->setSize (size.load());
            model->reset (maxModelBlock (model->getSampleRate()));
        }
        const juce::SpinLock::ScopedLockType sl (lock);
        retired.reset();
        pending = std::move (model);
        pendingSet = true;
    }

    /** A2 captures: Full (1) or Lite (0). Off the audio thread. */
    void setSize (double size01)
    {
        if (std::abs (size.exchange (size01) - size01) < 1.0e-9)
            return;
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto* m : { active.get(), pending.get() })
            if (m != nullptr)
                m->setSize (size01);
    }

    /** Frees a capture the audio thread swapped out (message thread). */
    void releaseRetired()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        retired.reset();
    }

    bool hasModel() const noexcept { return loaded.load(); }

    /** Audio thread: takes a pending capture over. Call at the top of every block. */
    void pickUp() noexcept
    {
        if (! pendingSet)
            return;
        if (lock.tryEnter())
        {
            retired = std::move (active);
            active = std::move (pending);
            pendingSet = false;
            lock.exit();
            loaded.store (active != nullptr);
            resetStream();
        }
    }

    /** Audio thread: a capture is running. */
    bool isActive() const noexcept { return active != nullptr; }

    /** Audio thread: the capture on the mono sum of `audio`, written to every channel (in place).
        `inGain` / `outGain` are the user's trims on top of the calibration. */
    void process (float* const* audio, int numCh, int numSamples, float inGain, float outGain) noexcept
    {
        if (active == nullptr)
        {
            for (int c = 0; c < numCh; ++c)
                juce::FloatVectorOperations::clear (audio[c], numSamples);
            return;
        }
        inGain *= calGain;
        outGain *= loudGain;
        juce::dsp::AudioBlock<float> block (audio, (size_t) numCh, (size_t) numSamples);
        juce::dsp::AudioBlock<float> up;
        float* os[2] { audio[0], numCh > 1 ? audio[1] : nullptr };
        int nOs = numSamples;
        if (oversampler != nullptr)
        {
            up = oversampler->processSamplesUp (block);
            nOs = (int) up.getNumSamples();
            for (int c = 0; c < numCh; ++c)
                os[c] = up.getChannelPointer ((size_t) c);
        }

        int nModel = 0;
        const int capacity = (int) modelIn.size();
        for (int i = 0; i < nOs; ++i)
        {
            const float x = (numCh > 1 ? 0.5f * (os[0][i] + os[1][i]) : os[0][i]) * inGain;
            down.push (x, [&] (float v) { if (nModel < capacity) modelIn[(size_t) nModel++] = v; });
        }
        if (nModel > 0)
            active->process (modelIn.data(), modelOut.data(), nModel);

        const int qSize = (int) outQueue.size();
        for (int k = 0; k < nModel; ++k)
            upRs.push (modelOut[(size_t) k] * outGain, [&] (float v)
            {
                if (qCount < qSize)
                {
                    outQueue[(size_t) ((qRead + qCount) % qSize)] = v;
                    ++qCount;
                }
            });
        for (int i = 0; i < nOs; ++i)
        {
            float v = 0.0f;
            if (qCount > 0)
            {
                v = outQueue[(size_t) qRead];
                qRead = (qRead + 1) % qSize;
                --qCount;
            }
            for (int c = 0; c < numCh; ++c)
                os[c][i] = v;
        }

        if (oversampler != nullptr)
            oversampler->processSamplesDown (block);
    }

private:
    struct PushResampler
    {
        double step = 1.0, pos = 0.0;   // input samples per output sample
        float h[4] {};
        void reset() noexcept { pos = 0.0; for (auto& v : h) v = 0.0f; }
        template <typename Emit>
        inline void push (float x, Emit&& emit) noexcept
        {
            h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = x;
            while (pos < 1.0)
            {
                const float t = (float) pos;
                // Catmull-Rom between h[1] and h[2]
                const float c0 = h[1], c1 = 0.5f * (h[2] - h[0]);
                const float c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
                const float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
                emit (((c3 * t + c2) * t + c1) * t + c0);
                pos += step;
            }
            pos -= 1.0;
        }
    };

    int maxModelBlock (double modelRate) const noexcept
    {
        return (int) std::ceil ((double) maxBlock * modelRate / fs) + 16;
    }

    void resetStream() noexcept
    {
        down.reset();
        upRs.reset();
        std::fill (outQueue.begin(), outQueue.end(), 0.0f);
        qRead = 0;
        qCount = 0;
        const double modelRate = active != nullptr ? active->getSampleRate() : 48000.0;
        down.step = fsOs / modelRate;
        upRs.step = modelRate / fsOs;
        // a little prefill absorbs the +-1 sample jitter of the rate conversion
        const int prefill = (int) std::ceil (fsOs / modelRate) * 3 + 4;
        qCount = juce::jmin (prefill, (int) outQueue.size());
        calGain = 1.0f;
        loudGain = 1.0f;
        if (active != nullptr && active->hasInputLevel())
            calGain = juce::Decibels::decibelsToGain ((float) juce::jlimit (-24.0, 24.0, kInputDbu - active->getInputLevelDbu()));
        if (normaliseLoudness && active != nullptr && active->hasLoudness())
            loudGain = juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 24.0f, -18.0f - (float) active->getLoudnessDb()));
    }

    const bool normaliseLoudness;
    double fs = 44100.0, fsOs = 176400.0;
    int maxBlock = 512, osLog2 = 2;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::SpinLock lock;
    std::unique_ptr<NamModel> active, pending, retired;
    bool pendingSet = false;
    std::atomic<bool> loaded { false };
    std::atomic<double> size { 1.0 };
    PushResampler down, upRs;
    std::vector<float> modelIn, modelOut, outQueue;
    int qRead = 0, qCount = 0;
    float calGain = 1.0f, loudGain = 1.0f;
};
