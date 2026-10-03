#pragma once

#include "DSPUtils.h"
#include <array>
#include <vector>

/**
 * The output's performance tricks, after VOLUME: FREEZE and STOP of the whole signal.
 *
 *  FREEZE  the moment it goes down, the last 400 ms of the output are caught and held as a pad:
 *          two streams of Hann-windowed grains (85 ms, half overlapping) read from random spots of
 *          the snapshot, so the pad does not loop audibly. The live signal keeps passing on top; the
 *          pad fades in over 30 ms and out over 150 ms.
 *  STOP    a tape stop of everything: the output is read from a 3 s buffer at a speed that falls to
 *          zero over FALL seconds (pitch falls with it) and comes back up over RISE seconds when
 *          released; the live signal fades in over the second half of the rise, so the return is a
 *          glide, not a cut.
 */
class OutputHold
{
public:
    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        juce::ignoreUnused (maxBlock);
        const int len = juce::nextPowerOfTwo ((int) std::ceil (3.2 * sr));
        for (auto& b : ring)
            b.assign ((size_t) len, 0.0f);
        mask = len - 1;
        write = 0;
        snapshotLen = (int) (0.4 * sr);
        grainLen = (int) (0.085 * sr);
        for (auto& b : snapshot)
            b.assign ((size_t) snapshotLen, 0.0f);
        for (auto& w : window)
            w.assign ((size_t) grainLen, 0.0f);
        for (int i = 0; i < grainLen; ++i)
            window[0][(size_t) i] = 0.5f - 0.5f * std::cos (swarm::kTwoPi * (float) i / (float) grainLen);
        freezeIn  = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        freezeOut = (float) (1.0 - std::exp (-1.0 / (0.15 * sr)));
        liveFade  = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : ring) std::fill (b.begin(), b.end(), 0.0f);
        write = 0;
        freezeGain = 0.0f;
        frozen = false;
        grainPos = { 0, grainLen / 2 };
        grainStart = { 0, 0 };
        stopSpeed = 1.0f;
        stopActive = false;
        liveMix = 1.0f;
        readPos = 0.0;
    }

    void setParams (bool freeze, bool stop, float fallSeconds, float riseSeconds = -1.0f) noexcept
    {
        wantFreeze = freeze;
        wantStop = stop;
        stopTime = juce::jmax (0.05f, fallSeconds);
        riseTime = riseSeconds > 0.0f ? juce::jmax (0.05f, riseSeconds) : stopTime;
    }

    bool isIdle() const noexcept { return ! wantFreeze && ! wantStop && freezeGain < 1.0e-4f && ! stopActive && liveMix >= 0.999f; }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, 2);
        // FREEZE: catch the snapshot on the way down
        if (wantFreeze && ! frozen)
        {
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < snapshotLen; ++i)
                    snapshot[(size_t) c][(size_t) i] = ring[(size_t) c][(size_t) ((write - snapshotLen + i) & mask)];
            frozen = true;
            grainPos = { 0, grainLen / 2 };
            for (auto& g : grainStart) g = randomStart();
        }
        if (! wantFreeze && freezeGain < 1.0e-4f)
            frozen = false;

        // STOP: the tape starts slowing from where the live signal is now
        if (wantStop && ! stopActive)
        {
            stopActive = true;
            stopSpeed = 1.0f;
            readPos = (double) write;
        }
        const float fallStep = 1.0f / (stopTime * (float) sampleRate), riseStep = 1.0f / (riseTime * (float) sampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            float live[2] { audio[0][i], numChannels > 1 ? audio[1][i] : audio[0][i] };
            for (int c = 0; c < numChannels; ++c)
                ring[(size_t) c][(size_t) write] = live[c];

            float out[2] { live[0], live[1] };

            // ---- STOP
            if (stopActive)
            {
                stopSpeed = juce::jlimit (0.0f, 1.0f, stopSpeed + (wantStop ? -fallStep : riseStep));
                readPos += stopSpeed;
                const float tape = std::sqrt (stopSpeed);
                // on the way back up the live signal fades in over the second half of the rise (equal power)
                const float back = wantStop ? 0.0f : juce::jlimit (0.0f, 1.0f, (stopSpeed - 0.5f) * 2.0f);
                const float wLive = std::sin (back * swarm::kPi * 0.5f), wTape = std::cos (back * swarm::kPi * 0.5f);
                for (int c = 0; c < numChannels; ++c)
                    out[c] = wTape * tape * readRing (c, readPos) + wLive * live[c];
                if (! wantStop && stopSpeed >= 0.999f)
                    stopActive = false;   // all live again
            }

            // ---- FREEZE: two grain streams over the snapshot, on top of the live signal
            freezeGain += (wantFreeze ? freezeIn : freezeOut) * ((wantFreeze ? 1.0f : 0.0f) - freezeGain);
            if (freezeGain > 1.0e-4f && frozen)
            {
                float pad[2] {};
                for (size_t s = 0; s < 2; ++s)
                {
                    const float w = window[0][(size_t) grainPos[s]];
                    for (int c = 0; c < numChannels; ++c)
                        pad[c] += w * snapshot[(size_t) c][(size_t) (grainStart[s] + grainPos[s])];
                    if (++grainPos[s] >= grainLen)
                    {
                        grainPos[s] = 0;
                        grainStart[s] = randomStart();
                    }
                }
                for (int c = 0; c < numChannels; ++c)
                    out[c] += freezeGain * pad[c];
            }

            for (int c = 0; c < numChannels; ++c)
                audio[c][i] = out[c];
            write = (write + 1) & mask;
        }
    }

private:
    float readRing (int c, double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const int i0 = (int) fl;
        const float frac = (float) (pos - fl);
        const auto& b = ring[(size_t) c];
        const float a = b[(size_t) (i0 & mask)];
        return a + frac * (b[(size_t) ((i0 + 1) & mask)] - a);
    }
    int randomStart() noexcept { return (int) (rng.nextFloat() * (float) juce::jmax (1, snapshotLen - grainLen - 1)); }

    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> ring, snapshot;
    std::array<std::vector<float>, 1> window;
    int mask = 0, write = 0, snapshotLen = 0, grainLen = 0;
    std::array<int, 2> grainPos {}, grainStart {};
    bool wantFreeze = false, wantStop = false, frozen = false, stopActive = false;
    float freezeGain = 0.0f, freezeIn = 0.01f, freezeOut = 0.001f, liveFade = 0.01f, liveMix = 1.0f;
    float stopSpeed = 1.0f, stopTime = 1.0f, riseTime = 1.0f;
    double readPos = 0.0;
    swarm::FastRandom rng { 0x0F0F0F0Fu };
};
