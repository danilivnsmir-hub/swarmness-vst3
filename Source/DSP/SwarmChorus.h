#pragma once

#include "DSPUtils.h"
#include <array>
#include <vector>

/**
 * SWARM: multi-voice stereo ensemble.
 *
 *  Classic : 4 voices, ~7 ms base delay, up to ~9 ms of sweep - from lush to seasick.
 *  Deep    : 8 voices, ~12 ms base delay, up to ~16 ms of sweep, strong filtered feedback
 *            and random rate drift per voice - a dense, detuned, metallic "swarm".
 *
 * The wet path is coloured like a bucket-brigade chorus (dark low-pass, soft saturation),
 * voices are spread across the stereo field and high-passed at 90 Hz so the low end stays
 * tight and mono-compatible. MIX follows a pedal-style law: at 50% both dry and wet are at
 * full level, above that the dry fades out towards pure vibrato.
 */
class SwarmChorus
{
public:
    static constexpr int kMaxVoices = 8;

    void prepare (double sr)
    {
        sampleRate = sr;
        const int needed = (int) std::ceil (sr * 0.06) + 8;
        bufferSize = juce::nextPowerOfTwo (needed);
        mask = bufferSize - 1;
        for (auto& b : buffers)
            b.assign ((size_t) bufferSize, 0.0f);

        mixSmoothed  .reset (sr, 0.03);
        depthSmoothed.reset (sr, 0.05);
        modeBlend    .reset (sr, 0.08);
        mixSmoothed  .setCurrentAndTargetValue (targetMix);
        depthSmoothed.setCurrentAndTargetValue (targetDepth);
        modeBlend    .setCurrentAndTargetValue (deep ? 1.0f : 0.0f);

        for (auto& f : lowCut)
        {
            f.setType (swarm::SVF::Type::highPass);
            f.setParams (sr, 90.0f, 0.7071f);
        }
        for (auto& f : bbdLp)
            f.setType (swarm::SVF::Type::lowPass);
        lastBbdHz = -1.0f;

        reset();
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        for (auto& f : lowCut)
            f.reset();
        for (auto& f : bbdLp)
            f.reset();
        writePos = 0;
        feedbackL = feedbackR = 0.0f;

        for (int v = 0; v < kMaxVoices; ++v)
        {
            phases[(size_t) v] = (float) v / (float) kMaxVoices;
            drift [(size_t) v] = 1.0f;
            driftTarget[(size_t) v] = 1.0f;
        }
    }

    void setParams (float rateHz, float depth01, float mix01, bool deepMode) noexcept
    {
        rate = rateHz;
        targetDepth = depth01;
        targetMix = mix01;
        deep = deepMode;
        depthSmoothed.setTargetValue (depth01);
        mixSmoothed.setTargetValue (mix01);
        modeBlend.setTargetValue (deepMode ? 1.0f : 0.0f);
    }

    bool isSilent() const noexcept { return mixSmoothed.getCurrentValue() < 1.0e-4f && ! mixSmoothed.isSmoothing(); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        if (isSilent() && targetMix < 1.0e-4f)
        {
            // Keep the delay line primed so re-engaging is seamless.
            for (int i = 0; i < numSamples; ++i)
            {
                buffers[0][(size_t) writePos] = audio[0][i];
                buffers[1][(size_t) writePos] = audio[numChannels > 1 ? 1 : 0][i];
                writePos = (writePos + 1) & mask;
            }
            return;
        }

        const float inc = rate / (float) sampleRate;

        // Occasionally retarget the per-voice rate drift (organic, non-periodic motion).
        driftCounter += numSamples;
        if (driftCounter > (int) (sampleRate * 0.25))
        {
            driftCounter = 0;
            for (auto& t : driftTarget)
                t = 1.0f + 0.12f * rng.nextBipolar();
        }

        // BBD darkness: brighter in classic mode, darker in deep mode
        const float bbdHz = 7000.0f - 2500.0f * modeBlend.getCurrentValue();
        if (std::abs (bbdHz - lastBbdHz) > 20.0f)
        {
            lastBbdHz = bbdHz;
            for (auto& f : bbdLp)
                f.setParams (sampleRate, bbdHz, 0.6f);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = audio[0][i];
            const float inR = numChannels > 1 ? audio[1][i] : inL;

            const float blend = modeBlend.getNextValue();      // 0 = classic, 1 = deep
            const float depth = depthSmoothed.getNextValue();
            const float mix   = mixSmoothed.getNextValue();

            const float fb = 0.1f + 0.3f * blend;
            buffers[0][(size_t) writePos] = inL + fb * feedbackL;
            buffers[1][(size_t) writePos] = inR + fb * feedbackR;

            const float baseMs  = 7.0f + 5.0f * blend;
            const float depthMs = (9.0f + 7.0f * blend) * depth * (0.3f + 0.7f * depth);
            const float baseSamples  = baseMs  * 0.001f * (float) sampleRate;
            const float depthSamples = depthMs * 0.001f * (float) sampleRate;

            float wetL = 0.0f, wetR = 0.0f;

            for (int v = 0; v < kMaxVoices; ++v)
            {
                // Voices 4..7 fade in with the Deep blend.
                const float voiceGain = v < 4 ? 1.0f : blend;

                auto& d = drift[(size_t) v];
                d += 0.00002f * (driftTarget[(size_t) v] - d);

                auto& ph = phases[(size_t) v];
                ph += inc * d * (1.0f + 0.07f * (float) v);
                ph -= std::floor (ph);

                if (voiceGain <= 0.0f)
                    continue;

                const float lfo = std::sin (swarm::kTwoPi * ph);
                const float delay = baseSamples * (1.0f + 0.13f * (float) v) + depthSamples * (0.5f + 0.5f * lfo);

                // Pan voices alternately left/right with varying width.
                const float pan = ((v & 1) == 0 ? -1.0f : 1.0f) * (0.35f + 0.65f * (float) (v / 2) / 3.0f);
                const float gl = std::sqrt (0.5f * (1.0f - pan));
                const float gr = std::sqrt (0.5f * (1.0f + pan));

                const float sL = read (0, delay);
                const float sR = read (1, delay);
                const float s  = 0.5f * (sL + sR) + 0.5f * pan * (sR - sL);

                wetL += voiceGain * gl * s;
                wetR += voiceGain * gr * s;
            }

            const float norm = 1.0f / std::sqrt (4.0f + 4.0f * blend);
            wetL *= norm * 1.414f;
            wetR *= norm * 1.414f;

            // Bucket-brigade colour: dark and slightly saturated (also inside the feedback loop)
            wetL = std::tanh (1.4f * bbdLp[0].process (wetL)) * (1.0f / 1.4f);
            wetR = std::tanh (1.4f * bbdLp[1].process (wetR)) * (1.0f / 1.4f);

            // Keep lows centred and clean: high-pass only the wet voices (feedback is taken
            // after the high-pass so the coherent low end can never run away).
            const float hpL = lowCut[0].process (wetL);
            const float hpR = lowCut[1].process (wetR);
            feedbackL = hpL;
            feedbackR = hpR;

            const float dryG = juce::jmin (1.0f, 2.0f * (1.0f - mix));
            const float wetG = juce::jmin (1.0f, 2.0f * mix) * 1.15f;

            audio[0][i] = dryG * inL + wetG * hpL;
            if (numChannels > 1)
                audio[1][i] = dryG * inR + wetG * hpR;

            writePos = (writePos + 1) & mask;
        }
    }

private:
    float read (int ch, float delay) const noexcept
    {
        const float readPos = (float) writePos - delay;
        const float fl = std::floor (readPos);
        const int i1 = (int) fl;
        const float frac = readPos - fl;
        const auto& b = buffers[(size_t) ch];
        return swarm::hermite (b[(size_t) ((i1 - 1) & mask)], b[(size_t) (i1 & mask)],
                               b[(size_t) ((i1 + 1) & mask)], b[(size_t) ((i1 + 2) & mask)], frac);
    }

    double sampleRate = 44100.0;
    std::array<std::vector<float>, 2> buffers;
    int bufferSize = 0, mask = 0, writePos = 0;

    std::array<float, kMaxVoices> phases {}, drift {}, driftTarget {};
    int driftCounter = 0;
    swarm::FastRandom rng { 0xC0FFEEu };

    std::array<swarm::SVF, 2> lowCut, bbdLp;
    float lastBbdHz = -1.0f;
    float feedbackL = 0.0f, feedbackR = 0.0f;

    float rate = 0.6f, targetDepth = 0.5f, targetMix = 0.0f;
    bool deep = false;
    juce::SmoothedValue<float> mixSmoothed, depthSmoothed, modeBlend;
};
