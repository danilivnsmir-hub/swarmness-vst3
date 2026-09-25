#pragma once

#include "DSPUtils.h"
#include "LivePitchShifter.h"
#include <array>

/**
 * Cheap-converter emulation for the RAW voices: a soft ADC overload, a band-limit, a
 * sample-and-hold rate reduction (with the aliasing that brings) and bit-depth reduction.
 */
class LoFiConverter
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& f : preLp)  { f.setType (swarm::SVF::Type::lowPass); f.reset(); }
        for (auto& f : postLp) { f.setType (swarm::SVF::Type::lowPass); f.reset(); }
        configure (rateHz, bits, bandHz);
        holdPhase = 1.0f;
        held = { 0.0f, 0.0f };
    }

    void configure (float targetRateHz, float bitDepth, float bandLimitHz) noexcept
    {
        rateHz = targetRateHz;
        bits = bitDepth;
        bandHz = bandLimitHz;
        holdStep = juce::jmin (1.0f, rateHz / (float) sampleRate);
        quant = std::pow (2.0f, bits - 1.0f);
        for (auto& f : preLp)  f.setParams (sampleRate, bandHz * 1.3f, 0.6f);
        for (auto& f : postLp) f.setParams (sampleRate, bandHz, 0.7071f);
    }

    /** Processes one frame (all channels) in place. */
    void processFrame (float* frame, int numChannels) noexcept
    {
        holdPhase += holdStep;
        const bool sample = holdPhase >= 1.0f;
        if (sample)
            holdPhase -= 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // ADC: gentle overload, then band-limit
            float x = preLp[(size_t) ch].process (std::tanh (1.4f * frame[ch]) * (1.0f / 1.4f));
            if (sample)
                held[(size_t) ch] = std::round (x * quant) / quant;
            frame[ch] = postLp[(size_t) ch].process (held[(size_t) ch]);
        }
    }

private:
    double sampleRate = 44100.0;
    float rateHz = 24000.0f, bits = 12.0f, bandHz = 9000.0f;
    float holdStep = 0.5f, holdPhase = 1.0f, quant = 2048.0f;
    std::array<float, 2> held {};
    std::array<swarm::SVF, 2> preLp, postLp;
};

//==============================================================================
/**
 * One harmony / octave voice. The pitch itself always comes from the splice-aligned
 * LivePitchShifter, so it stays in tune and on the pick. RAW adds the character of cheap
 * pedal DSPs on top: a slow random pitch warble (like FV-1-era shifters), rougher splices and
 * cheap-converter emulation. Switching RAW crossfades over 30 ms.
 *
 * (An FV-1-style free-running crossfade shifter was tried: with windows short enough to stay
 * on the pick it drifts 20-50 cents out of tune, which turns stacked voices into mush.)
 */
class PitchVoice
{
public:
    static constexpr int kMaxChunk = 64;

    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        shifter.prepare (sr, numChannels);
        lofi.prepare (sr);
        warbleSmoother.setTime (sr / kMaxChunk, 0.08);
        rawMix = rawTarget ? 1.0f : 0.0f;
        applyTightness();
    }

    void reset()
    {
        shifter.reset();
        lofi.prepare (sampleRate);
        warbleSmoother.reset (0.0f);
        warbleCounter = 0;
        rawMix = rawTarget ? 1.0f : 0.0f;
    }

    void setTightness (float t) noexcept            { tightness = t; applyTightness(); }
    void setRaw (bool raw) noexcept                  { if (raw != rawTarget) { rawTarget = raw; applyTightness(); } }
    void setLoFi (float rateHz, float bits, float bandHz) noexcept { lofi.configure (rateHz, bits, bandHz); }
    /** RAW pitch warble: depth in cents, rate of new random targets in Hz; roughness 0..1 loosens the splices. */
    void setRawCharacter (float warbleCents, float warbleRateHz, float roughness01) noexcept
    {
        warbleDepth = warbleCents;
        warbleRate = juce::jmax (0.5f, warbleRateHz);
        roughness = juce::jlimit (0.0f, 1.0f, roughness01);
        applyTightness();
    }
    bool isRaw() const noexcept                      { return rawMix > 0.5f; }

    void process (float* const* channels, int numChannels, int numSamples, float ratioStart, float ratioEnd) noexcept
    {
        numChannels = juce::jmin (numChannels, 2);
        for (int start = 0; start < numSamples; start += kMaxChunk)
        {
            const int n = juce::jmin (kMaxChunk, numSamples - start);
            const float r0 = ratioStart + (ratioEnd - ratioStart) * (float) start / (float) numSamples;
            const float r1 = ratioStart + (ratioEnd - ratioStart) * (float) (start + n) / (float) numSamples;
            float* sub[2] = { channels[0] + start, channels[numChannels > 1 ? 1 : 0] + start };
            processChunk (sub, numChannels, n, r0, r1);
        }
    }

private:
    void applyTightness() noexcept
    {
        shifter.setTightness (rawTarget ? tightness * (1.0f - 0.45f * roughness) : tightness);
    }

    void processChunk (float* const* ch, int numChannels, int n, float r0, float r1) noexcept
    {
        const float step = (float) n / (float) (0.03 * sampleRate);
        const float mixStart = rawMix;
        rawMix = rawTarget ? juce::jmin (1.0f, rawMix + step) : juce::jmax (0.0f, rawMix - step);
        const float mixEnd = rawMix;

        // Warble (scaled by the RAW amount so switching is smooth)
        warbleCounter += n;
        if (warbleCounter >= (int) (sampleRate / warbleRate))
        {
            warbleCounter = 0;
            warbleTarget = rng.nextBipolar() * warbleDepth;
        }
        const float cents = warbleSmoother.process (warbleTarget) * mixEnd;
        const float w = std::pow (2.0f, cents / 1200.0f);
        shifter.process (ch, numChannels, n, r0 * lastWarble, r1 * w);
        lastWarble = w;

        if (mixStart <= 0.0f && mixEnd <= 0.0f)
            return;

        float frame[2];
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
                frame[c] = ch[c][i];
            lofi.processFrame (frame, numChannels);

            const float m = mixStart + (mixEnd - mixStart) * (float) (i + 1) / (float) n;
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] = m >= 1.0f ? frame[c] : ch[c][i] + m * (frame[c] - ch[c][i]);
        }
    }

    double sampleRate = 44100.0;
    LivePitchShifter shifter;
    LoFiConverter lofi;
    bool rawTarget = true;
    float rawMix = 1.0f, tightness = 1.0f, roughness = 0.0f;

    float warbleDepth = 0.0f, warbleRate = 5.0f, warbleTarget = 0.0f, lastWarble = 1.0f;
    int warbleCounter = 0;
    swarm::OnePole warbleSmoother;
    swarm::FastRandom rng { 0xB0BACAFEu };
};
