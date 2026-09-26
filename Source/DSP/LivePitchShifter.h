#pragma once

#include "DSPUtils.h"
#include <array>
#include <vector>

/**
 * Input ring shared by several pitch-shifter taps.
 *
 * Voices that shift the same signal (the footswitch SHIFT and its ANGER voice; DRONE and QUEEN)
 * read one ring instead of keeping a copy each - less memory and one write per sample.
 * Usage per chunk:  base = ring.write (input);  tap.process (out, ..., base, ratios);
 */
class ShiftRing
{
public:
    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        size = sw::nextPowerOfTwo ((int) std::ceil (sr * 0.7) + 16);
        mask = size - 1;
        buffers.assign ((size_t) sw::jmax (1, numChannels), std::vector<float> ((size_t) size, 0.0f));
        writePos = 0;
    }

    void clear() noexcept
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
    }

    /** Appends a chunk; returns the ring position of its first sample. */
    int write (const float* const* in, int numChannels, int numSamples) noexcept
    {
        const int base = writePos;
        numChannels = sw::jmin (numChannels, getNumChannels());
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffers[(size_t) ch][(size_t) writePos] = in[ch][i];
            writePos = (writePos + 1) & mask;
        }
        return base;
    }

    int getNumChannels() const noexcept { return (int) buffers.size(); }
    float at (int ch, int pos) const noexcept { return buffers[(size_t) ch][(size_t) (pos & mask)]; }
    const std::vector<float>& channel (int ch) const noexcept { return buffers[(size_t) ch]; }

    double sampleRate = 44100.0;
    int size = 0, mask = 0, writePos = 0;

private:
    std::vector<std::vector<float>> buffers;
};

//==============================================================================
/**
 * Live pitch shifter tap with splice-aligned jumps (reads a ShiftRing).
 *
 * The read tap moves through the ring at the pitch ratio; when it gets too close to the write
 * head (up-shift) or too far behind (down-shift) it jumps by a multiple of the waveform's period,
 * found by normalised cross-correlation, and crossfades. The alignment keeps it in tune and on
 * the pick with no look-ahead (zero latency).
 *
 * The correlation search is two-stage: a coarse pass (every 4th candidate / tap) finds the
 * region, a fine pass refines it - about ten times cheaper than a full search, so it also fits
 * embedded CPUs (a splice costs a few percent of a 1 ms block on a 480 MHz Cortex-M7).
 */
class LivePitchShifter
{
public:
    void prepare (const ShiftRing& r)
    {
        ring = &r;
        sampleRate = r.sampleRate;
        mask = r.mask;

        corrLength = sw::jmax (32, (int) (sampleRate * 0.012));
        corrStep   = sampleRate > 60000.0 ? 4 : 2;
        maxDelay   = (float) r.size - (float) corrLength - 8.0f;
        reference.assign ((size_t) (corrLength / corrStep + 2), 0.0f);
        setTightness (tightness);
        reset();
    }

    /**
     * 1 = tight: short, phase-aligned splices (clean tracking).
     * 0 = loose: long, unaligned jumps that repeat fragments ("tracking" glitches / tone clusters).
     */
    void setTightness (float t) noexcept
    {
        tightness = sw::jlimit (0.0f, 1.0f, t);
        const float loose = 1.0f - tightness;
        const auto ms = [this] (float v) { return (float) (sampleRate * v * 0.001); };
        fadeLength     = sw::jmax (16, (int) ms (10.0f + 18.0f * loose));
        searchRange    = ms (8.0f);
        downJump       = ms (28.0f + 170.0f * loose * loose);
        minUpJump      = ms (18.0f + 160.0f * loose * loose);
        alignSplices   = tightness > 0.3f;
    }

    void reset() noexcept
    {
        delay = { minDelay + 1.0f, minDelay + 1.0f };
        active = 0;
        fading = false;
        fadePos = 0;
        unityBlend = 0.0f;
    }

    /**
     * Writes numSamples shifted samples to out. base = ring position of the chunk's first input
     * sample (returned by ShiftRing::write for this chunk).
     */
    void process (float* const* out, int numChannels, int numSamples, int base, float ratioStart, float ratioEnd) noexcept
    {
        numChannels = sw::jmin (numChannels, ring->getNumChannels());
        const float ratioStep = (ratioEnd - ratioStart) / (float) sw::jmax (1, numSamples);

        // At exactly unity, fade to the direct signal (no splices at all).
        const bool unity = std::abs (ratioStart - 1.0f) < 1.0e-4f && std::abs (ratioEnd - 1.0f) < 1.0e-4f;
        const float unityTarget = unity ? 1.0f : 0.0f;

        float ratio = ratioStart;

        for (int i = 0; i < numSamples; ++i)
        {
            const int wp = (base + i) & mask;

            const float rate = 1.0f - ratio;   // delay change per sample
            delay[0] += rate;
            delay[1] += rate;

            if (! fading)
                maybeSplice (ratio, numChannels, wp);

            float gOld = 1.0f, gNew = 0.0f;
            if (fading)
            {
                const float t = (float) fadePos / (float) fadeLength;
                gNew = 0.5f - 0.5f * std::cos (swarm::kPi * t);
                gOld = 1.0f - gNew;
                if (++fadePos >= fadeLength)
                {
                    fading = false;
                    active = 1 - active;
                }
            }

            const int other = 1 - active;
            unityBlend += 0.0015f * (unityTarget - unityBlend);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto& buf = ring->channel (ch);
                float shifted = gOld * readAt (buf, wp, delay[(size_t) active]);
                if (gNew > 0.0f)
                    shifted += gNew * readAt (buf, wp, delay[(size_t) other]);

                out[ch][i] = unityBlend > 1.0e-5f ? shifted + unityBlend * (buf[(size_t) wp] - shifted) : shifted;
            }

            ratio += ratioStep;
        }
    }

private:
    void maybeSplice (float ratio, int numChannels, int wp) noexcept
    {
        const float d = delay[(size_t) active];
        const float fadeTravel = std::abs (1.0f - ratio) * (float) fadeLength;

        float target = d;
        if (ratio > 1.0f)
        {
            // Tap approaches the write head: jump further back in time.
            if (d > minDelay + fadeTravel * 1.1f + 1.0f)
                return;
            target = d + sw::jmax (minUpJump, fadeTravel * 1.5f + minUpJump * 0.5f);
        }
        else if (ratio < 1.0f)
        {
            // Tap falls behind: jump forward in time.
            if (d < minDelay + downJump + searchRange + fadeTravel)
                return;
            target = d - downJump;
        }
        else
        {
            return;
        }

        target = sw::jlimit (minDelay + searchRange + fadeTravel, maxDelay - searchRange, target);
        delay[(size_t) (1 - active)] = alignSplices ? findBestDelay (d, target, numChannels, wp) : target;
        fading = true;
        fadePos = 0;
    }

    /** Searches target +/- searchRange for the delay whose recent waveform best matches the current tap. */
    float findBestDelay (float current, float target, int numChannels, int wp) noexcept
    {
        const int cur = (int) std::round (current);

        // The current tap's recent waveform (channels summed), and its energy - computed once.
        const int taps = corrLength / corrStep;
        float ea = 0.0f;
        for (int k = 0; k < taps; ++k)
        {
            float a = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                a += ring->at (ch, wp - cur - k * corrStep);
            reference[(size_t) k] = a;
            ea += a * a;
        }
        if (ea < 1.0e-12f)
            return target + (current - (float) cur);

        const auto score = [&] (int c, int tapStride) -> float
        {
            float num = 0.0f, eb = 0.0f;
            for (int k = 0; k < taps; k += tapStride)
            {
                float b = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    b += ring->at (ch, wp - c - k * corrStep);
                num += reference[(size_t) k] * b;
                eb  += b * b;
            }
            return eb > 1.0e-12f ? num / std::sqrt (eb) : 0.0f;   // ea is common to all candidates
        };

        // Coarse: every 4th candidate and every 4th tap
        const int coarse = corrStep * 4;
        const int lo = (int) std::round (target - searchRange), hi = (int) std::round (target + searchRange);
        int best = (int) std::round (target);
        float bestScore = -1.0e30f;
        for (int c = lo; c <= hi; c += coarse)
        {
            const float s = score (c, 4);
            if (s > bestScore) { bestScore = s; best = c; }
        }

        // Fine: full resolution around the coarse winner
        const int centre = best;
        bestScore = -1.0e30f;
        for (int c = sw::jmax (lo, centre - coarse); c <= sw::jmin (hi, centre + coarse); c += corrStep)
        {
            const float s = score (c, 1);
            if (s > bestScore) { bestScore = s; best = c; }
        }

        return (float) best + (current - (float) cur);   // keep the fractional part continuous
    }

    float readAt (const std::vector<float>& buf, int wp, float d) const noexcept
    {
        const float readPos = (float) wp - d;
        const float fl = std::floor (readPos);
        const int i1 = (int) fl;
        const float frac = readPos - fl;
        return swarm::hermite (buf[(size_t) ((i1 - 1) & mask)], buf[(size_t) (i1 & mask)],
                               buf[(size_t) ((i1 + 1) & mask)], buf[(size_t) ((i1 + 2) & mask)], frac);
    }

    const ShiftRing* ring = nullptr;
    double sampleRate = 44100.0;
    int mask = 0;
    std::vector<float> reference;

    std::array<float, 2> delay { 4.0f, 4.0f };
    int active = 0;
    bool fading = false;
    int fadePos = 0;
    float unityBlend = 0.0f;

    float tightness = 1.0f;
    bool alignSplices = true;
    float minDelay = 3.0f, searchRange = 384.0f, downJump = 1344.0f, minUpJump = 864.0f, maxDelay = 10000.0f;
    int fadeLength = 480, corrLength = 576, corrStep = 2;
};
