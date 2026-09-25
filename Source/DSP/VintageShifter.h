#pragma once

#include "DSPUtils.h"
#include "LivePitchShifter.h"
#include <array>
#include <vector>

/**
 * "RAW" pitch shifter in the style of cheap 90s-2010s pedal DSPs (e.g. the Spin FV-1 inside
 * many boutique pitch pedals): two read taps ramp through a delay line half a window apart
 * and are continuously crossfaded with complementary sin^2 windows. No splice alignment -
 * the constant crossfading is what gives these pedals their warble, chorusing and buzzy,
 * grainy octaves.
 */
class VintageShifter
{
public:
    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        bufferSize = juce::nextPowerOfTwo ((int) std::ceil (sr * 0.35) + 16);
        mask = bufferSize - 1;
        buffers.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> ((size_t) bufferSize, 0.0f));
        targetWindow = window = (float) (0.04 * sr);
        reset();
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0;
        phase = 0.0f;
        unityBlend = 0.0f;
    }

    /**
     * Crossfade rate in Hz: the window is sized so the taps wrap this many times per second for
     * the current shift (window = |1 - ratio| / rate), clamped to 15..280 ms. Low rates = slow
     * warble / chorus and lag, high rates = buzzy, grainy ring-mod artefacts.
     */
    void setModulationRate (float hz) noexcept { modRate = juce::jlimit (1.0f, 60.0f, hz); }

    /** Writes the input without producing output (keeps the delay line current while unused). */
    void pushOnly (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, (int) buffers.size());
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffers[(size_t) ch][(size_t) writePos] = channels[ch][i];
            writePos = (writePos + 1) & mask;
        }
    }

    void process (float* const* channels, int numChannels, int numSamples, float ratioStart, float ratioEnd) noexcept
    {
        numChannels = juce::jmin (numChannels, (int) buffers.size());
        const float ratioStep = (ratioEnd - ratioStart) / (float) juce::jmax (1, numSamples);
        const bool unity = std::abs (ratioStart - 1.0f) < 1.0e-4f && std::abs (ratioEnd - 1.0f) < 1.0e-4f;
        const float unityTarget = unity ? 1.0f : 0.0f;
        float ratio = ratioStart;

        const float shift = std::abs (1.0f - 0.5f * (ratioStart + ratioEnd));
        const float maxWindow = juce::jmin ((float) (0.28 * sampleRate), (float) bufferSize - 64.0f);
        targetWindow = juce::jlimit ((float) (0.015 * sampleRate), maxWindow, shift / modRate * (float) sampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffers[(size_t) ch][(size_t) writePos] = channels[ch][i];

            window += 0.001f * (targetWindow - window);

            // Both taps move at (1 - ratio) samples per sample; the phase wraps once per window.
            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);
            float phaseB = phase + 0.5f;
            phaseB -= std::floor (phaseB);

            const float sA = std::sin (swarm::kPi * phase);
            const float sB = std::sin (swarm::kPi * phaseB);
            const float gA = sA * sA, gB = sB * sB;                  // gA + gB == 1
            const float dA = kMinDelay + phase  * window;
            const float dB = kMinDelay + phaseB * window;

            unityBlend += 0.0015f * (unityTarget - unityBlend);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto& buf = buffers[(size_t) ch];
                const float shifted = gA * readAt (buf, dA) + gB * readAt (buf, dB);
                channels[ch][i] = unityBlend > 1.0e-5f ? shifted + unityBlend * (channels[ch][i] - shifted) : shifted;
            }

            writePos = (writePos + 1) & mask;
            ratio += ratioStep;
        }
    }

private:
    static constexpr float kMinDelay = 4.0f;

    float readAt (const std::vector<float>& buf, float d) const noexcept
    {
        const float readPos = (float) writePos - d;
        const float fl = std::floor (readPos);
        const int i0 = (int) fl;
        const float frac = readPos - fl;
        const float a = buf[(size_t) (i0 & mask)];
        return a + frac * (buf[(size_t) ((i0 + 1) & mask)] - a);     // linear, like the chips
    }

    double sampleRate = 44100.0;
    std::vector<std::vector<float>> buffers;
    int bufferSize = 0, mask = 0, writePos = 0;
    float modRate = 10.0f, targetWindow = 1764.0f, window = 1764.0f;
    float phase = 0.0f, unityBlend = 0.0f;
};

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
 * One harmony / octave voice with two engines: the clean, splice-aligned LivePitchShifter
 * ("modern") and the VintageShifter + LoFiConverter ("RAW"). Switching crossfades over 30 ms;
 * the unused engine keeps receiving input so it is ready to take over.
 */
class PitchVoice
{
public:
    static constexpr int kMaxChunk = 64;

    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        modern.prepare (sr, numChannels);
        vintage.prepare (sr, numChannels);
        lofi.prepare (sr);
        rawMix = rawTarget ? 1.0f : 0.0f;
    }

    void reset()
    {
        modern.reset();
        vintage.reset();
        lofi.prepare (sampleRate);
        rawMix = rawTarget ? 1.0f : 0.0f;
    }

    void setTightness (float t) noexcept           { modern.setTightness (t); }
    void setRaw (bool raw) noexcept                 { rawTarget = raw; }
    void setVintageModulationRate (float hz) noexcept { vintage.setModulationRate (hz); }
    void setLoFi (float rateHz, float bits, float bandHz) noexcept { lofi.configure (rateHz, bits, bandHz); }
    bool isRaw() const noexcept                     { return rawMix > 0.5f; }

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
    void processChunk (float* const* ch, int numChannels, int n, float r0, float r1) noexcept
    {
        const float step = (float) n / (float) (0.03 * sampleRate);
        const float mixStart = rawMix;
        rawMix = rawTarget ? juce::jmin (1.0f, rawMix + step) : juce::jmax (0.0f, rawMix - step);
        const float mixEnd = rawMix;

        if (mixStart <= 0.0f && mixEnd <= 0.0f)
        {
            vintage.pushOnly (ch, numChannels, n);
            modern.process (ch, numChannels, n, r0, r1);
            return;
        }

        float* raw[2] = { temp[0].data(), temp[1].data() };
        for (int c = 0; c < numChannels; ++c)
            std::copy (ch[c], ch[c] + n, raw[c]);

        if (mixStart >= 1.0f && mixEnd >= 1.0f)
            modern.pushOnly (ch, numChannels, n);
        else
            modern.process (ch, numChannels, n, r0, r1);

        vintage.process (raw, numChannels, n, r0, r1);

        float frame[2];
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
                frame[c] = raw[c][i];
            lofi.processFrame (frame, numChannels);

            const float m = mixStart + (mixEnd - mixStart) * (float) (i + 1) / (float) n;
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] = m >= 1.0f ? frame[c] : ch[c][i] + m * (frame[c] - ch[c][i]);
        }
    }

    double sampleRate = 44100.0;
    LivePitchShifter modern;
    VintageShifter vintage;
    LoFiConverter lofi;
    bool rawTarget = true;
    float rawMix = 1.0f;
    std::array<std::array<float, kMaxChunk>, 2> temp {};
};
