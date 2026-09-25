#pragma once

#include "DSPUtils.h"
#include <array>
#include <vector>

/**
 * Low-latency time-domain pitch shifter ("LIVE" quality).
 *
 * A single read tap sweeps through a delay line at the transposition ratio, so most of the
 * time the output is a clean resampled copy of the input at exactly the target pitch.
 * When the tap runs out of room it splices to a new position one "jump" away; the new
 * position is chosen by waveform cross-correlation so the splice lands in phase (on a pitch
 * period boundary), and a short raised-cosine crossfade hides the transition.
 * No look-ahead: the plug-in reports zero latency for this engine.
 */
class LivePitchShifter
{
public:
    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        bufferSize = juce::nextPowerOfTwo ((int) std::ceil (sr * 0.7) + 16);
        mask = bufferSize - 1;
        buffers.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> ((size_t) bufferSize, 0.0f));

        corrLength   = juce::jmax (32, (int) (sr * 0.012));
        corrStep     = sr > 60000.0 ? 4 : 2;
        maxDelay     = (float) bufferSize - (float) corrLength - 8.0f;
        setTightness (tightness);

        reset();
    }

    /**
     * 1 = tight: short, phase-aligned splices (clean tracking).
     * 0 = loose: long, unaligned jumps that repeat fragments ("tracking" glitches / tone clusters).
     */
    void setTightness (float t) noexcept
    {
        tightness = juce::jlimit (0.0f, 1.0f, t);
        const float loose = 1.0f - tightness;
        const auto ms = [this] (float v) { return (float) (sampleRate * v * 0.001); };
        fadeLength     = juce::jmax (16, (int) ms (10.0f + 18.0f * loose));
        searchRange    = ms (8.0f);
        downJump       = ms (28.0f + 170.0f * loose * loose);
        minUpJump      = ms (18.0f + 160.0f * loose * loose);
        alignSplices   = tightness > 0.3f;
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0;
        delay = { minDelay + 1.0f, minDelay + 1.0f };
        active = 0;
        fading = false;
        fadePos = 0;
        unityBlend = 0.0f;
    }

    void process (float* const* channels, int numChannels, int numSamples, float ratioStart, float ratioEnd) noexcept
    {
        numChannels = juce::jmin (numChannels, (int) buffers.size());
        const float ratioStep = (ratioEnd - ratioStart) / (float) juce::jmax (1, numSamples);

        // At exactly unity, fade to the direct signal (no splices at all).
        const bool unity = std::abs (ratioStart - 1.0f) < 1.0e-4f && std::abs (ratioEnd - 1.0f) < 1.0e-4f;
        const float unityTarget = unity ? 1.0f : 0.0f;

        float ratio = ratioStart;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffers[(size_t) ch][(size_t) writePos] = channels[ch][i];

            const float rate = 1.0f - ratio;   // delay change per sample
            delay[0] += rate;
            delay[1] += rate;

            if (! fading)
                maybeSplice (ratio, numChannels);

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
                const auto& buf = buffers[(size_t) ch];
                float shifted = gOld * readAt (buf, delay[(size_t) active]);
                if (gNew > 0.0f)
                    shifted += gNew * readAt (buf, delay[(size_t) other]);

                channels[ch][i] = unityBlend > 1.0e-5f ? shifted + unityBlend * (channels[ch][i] - shifted) : shifted;
            }

            writePos = (writePos + 1) & mask;
            ratio += ratioStep;
        }
    }

private:
    void maybeSplice (float ratio, int numChannels) noexcept
    {
        const float d = delay[(size_t) active];
        const float fadeTravel = std::abs (1.0f - ratio) * (float) fadeLength;

        float target = d;
        if (ratio > 1.0f)
        {
            // Tap approaches the write head: jump further back in time.
            if (d > minDelay + fadeTravel * 1.1f + 1.0f)
                return;
            target = d + juce::jmax (minUpJump, fadeTravel * 1.5f + minUpJump * 0.5f);
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

        target = juce::jlimit (minDelay + searchRange + fadeTravel, maxDelay - searchRange, target);
        delay[(size_t) (1 - active)] = alignSplices ? findBestDelay (d, target, numChannels) : target;
        fading = true;
        fadePos = 0;
    }

    /** Searches target +/- searchRange for the delay whose recent waveform best matches the current tap. */
    float findBestDelay (float current, float target, int numChannels) const noexcept
    {
        const int cur = (int) std::round (current);
        float best = target, bestScore = -2.0f;

        for (float cand = target - searchRange; cand <= target + searchRange; cand += (float) corrStep)
        {
            const int c = (int) std::round (cand);
            double num = 0.0, ea = 0.0, eb = 0.0;

            for (int u = 0; u < corrLength; u += corrStep)
            {
                float a = 0.0f, b = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto& buf = buffers[(size_t) ch];
                    a += buf[(size_t) ((writePos - cur - u) & mask)];
                    b += buf[(size_t) ((writePos - c   - u) & mask)];
                }
                num += (double) a * b;
                ea  += (double) a * a;
                eb  += (double) b * b;
            }

            const float score = (ea > 1.0e-12 && eb > 1.0e-12) ? (float) (num / std::sqrt (ea * eb)) : 0.0f;
            if (score > bestScore)
            {
                bestScore = score;
                best = (float) c + (current - (float) cur);   // keep the fractional part continuous
            }
        }
        return best;
    }

    float readAt (const std::vector<float>& buf, float d) const noexcept
    {
        const float readPos = (float) writePos - d;
        const float fl = std::floor (readPos);
        const int i1 = (int) fl;
        const float frac = readPos - fl;
        return swarm::hermite (buf[(size_t) ((i1 - 1) & mask)], buf[(size_t) (i1 & mask)],
                               buf[(size_t) ((i1 + 1) & mask)], buf[(size_t) ((i1 + 2) & mask)], frac);
    }

    double sampleRate = 44100.0;
    std::vector<std::vector<float>> buffers;
    int bufferSize = 0, mask = 0, writePos = 0;

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
