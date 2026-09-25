#pragma once

#include "DSPUtils.h"
#include "LivePitchShifter.h"
#include <array>
#include <vector>

/**
 * RAINBOW: two harmony voices with regeneration, inspired by EarthQuaker's Rainbow Machine.
 *
 *  PITCH     : interval of the primary voice, -12..+12 st, continuous (atonal) or snapped
 *  PRIMARY   : level of the primary voice (added to the input)
 *  SECONDARY : an octave of the primary (above when PITCH >= 0, below otherwise)
 *  TONE      : low-pass on the voices and inside the feedback loop
 *  TRACKING  : high = tight harmonies; low = lag and long, repeating grains (tone clusters)
 *  MAGIC     : feeds the voices back into the shifters: cascading "pixie trails", resonance,
 *              and past ~85% controllable self-oscillation (soft-limited, never explodes)
 */
class RainbowStage
{
public:
    static constexpr int kControlBlock = 32;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        primary  .prepare (sr, 2);
        secondary.prepare (sr, 2);

        const int maxLag = (int) std::ceil (sr * 0.2) + 8;
        lagLine.setMaximumDelayInSamples (maxLag);
        lagLine.prepare ({ sr, (juce::uint32) juce::jmax (maxBlockSize, kControlBlock), 2 });

        const int size = juce::jmax (maxBlockSize, kControlBlock);
        primBuf.setSize (2, size, false, false, true);
        secBuf .setSize (2, size, false, false, true);

        onSmoothed.reset (sr, 0.03);   primLevel.reset (sr, 0.03);
        secLevel.reset (sr, 0.03);     magicGain.reset (sr, 0.05);
        lagSmoothed.reset (sr, 0.15);
        onSmoothed.setCurrentAndTargetValue (0.0f);
        lagSmoothed.setCurrentAndTargetValue (0.0f);

        loopSize = juce::nextPowerOfTwo ((int) std::ceil (sr * 0.4) + 64);
        loopMask = loopSize - 1;
        for (auto& b : loopBuf)
            b.assign ((size_t) loopSize, 0.0f);
        inBuf.setSize (2, size, false, false, true);
        loopDelay.setTime (sr / kControlBlock, 0.2);
        loopDelay.reset (loopDelayTarget);

        for (auto& d : dc) d.prepare (sr);
        reset();
    }

    void reset()
    {
        primary.reset();
        secondary.reset();
        lagLine.reset();
        clearLoop();
        toneState = {};
        loopTone = { 0.0f, 0.0f };
        for (auto& d : dc) d.reset();
        lastPrimRatio = lastSecRatio = 1.0f;
    }

    void setParams (bool on, float pitchSemis, float primary01, float secondary01, float tone01,
                    float tracking01, float magic01, bool magicHeld) noexcept
    {
        onSmoothed.setTargetValue (on ? 1.0f : 0.0f);
        pitch = pitchSemis;
        primLevel.setTargetValue (primary01);
        secLevel .setTargetValue (secondary01);
        toneCoeff = std::exp (-swarm::kTwoPi * (400.0f * std::pow (40.0f, tone01)) / (float) sampleRate);

        tracking = tracking01;
        primary  .setTightness (tracking01);
        secondary.setTightness (tracking01);
        const float loose = 1.0f - tracking01;
        lagSmoothed.setTargetValue (loose * loose * 0.12f * (float) sampleRate);
        loopDelayTarget = (0.025f + 0.15f * loose) * (float) sampleRate;

        // Loop gain > 1 => self-oscillation (tanh-limited). The footswitch slams it past the knob's maximum.
        magicGain.setTargetValue (magicHeld ? 1.45f : magic01 * 1.3f);
        // The unshifted (resonant delay) part of the loop only comes in towards the top of the range:
        // mid settings give climbing / falling trails, the top end tips into self-oscillation.
        const float m = magicHeld ? 1.0f : magic01;
        resonance = 0.75f * m * m;
    }

    bool isActive() const noexcept { return onSmoothed.getCurrentValue() > 0.0f || onSmoothed.isSmoothing(); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        if (! isActive())
        {
            // Keep the loop quiet so re-engaging starts clean.
            if (! loopCleared)
                clearLoop();
            return;
        }

        loopCleared = false;
        numChannels = juce::jmin (numChannels, 2);
        const float primRatio = std::pow (2.0f, pitch / 12.0f);
        const float secRatio  = pitch >= 0.0f ? primRatio * 2.0f : primRatio * 0.5f;

        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = juce::jmin (kControlBlock, numSamples - start);
            const float loopD = juce::jmax ((float) (kControlBlock + 2), loopDelay.process (loopDelayTarget));

            // Shifter input = (lagged) input + the regeneration loop (a resonant delay that also
            // carries the shifted voices, so every repeat climbs / falls another interval).
            for (int i = 0; i < n; ++i)
            {
                const float lag = lagSmoothed.getNextValue();
                const float g = magicGain.getNextValue();
                lagLine.setDelay (lag);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    lagLine.pushSample (ch, audio[ch][start + i]);
                    const float x = lagLine.popSample (ch);
                    const float fb = readLoop (ch, (float) (loopWrite + i) - loopD);
                    const float in = x + g * 0.5f * std::tanh (2.0f * fb);   // same small-signal gain, lower ceiling
                    inBuf  .setSample (ch, i, in);
                    primBuf.setSample (ch, i, in);
                    secBuf .setSample (ch, i, in);
                }
            }

            float* p[2] = { primBuf.getWritePointer (0), primBuf.getWritePointer (numChannels > 1 ? 1 : 0) };
            float* s[2] = { secBuf .getWritePointer (0), secBuf .getWritePointer (numChannels > 1 ? 1 : 0) };
            primary  .process (p, numChannels, n, lastPrimRatio, primRatio);
            secondary.process (s, numChannels, n, lastSecRatio,  secRatio);
            lastPrimRatio = primRatio;
            lastSecRatio  = secRatio;

            for (int i = 0; i < n; ++i)
            {
                const float on = onSmoothed.getNextValue();
                const float pl = primLevel.getNextValue();
                const float sl = secLevel.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float pv = primBuf.getSample (ch, i);
                    const float sv = secBuf .getSample (ch, i);

                    // Tone: one-pole low-pass on the audible voices
                    const float voices = pv * pl + sv * sl;
                    auto& z = toneState[(size_t) ch];
                    z = voices + toneCoeff * (z - voices);

                    // Loop: shifted voices (independent of output levels, so MAGIC always regenerates)
                    // plus the unshifted input -> resonance; unity-plus gain gives self-oscillation.
                    const float fbIn = 0.8f * (pv + 0.7f * sv * juce::jmin (1.0f, sl * 4.0f)) + resonance * inBuf.getSample (ch, i);
                    auto& lt = loopTone[(size_t) ch];
                    lt = fbIn + toneCoeff * (lt - fbIn);
                    loopBuf[(size_t) ch][(size_t) ((loopWrite + i) & loopMask)] = dc[(size_t) ch].process (lt);

                    audio[ch][start + i] += on * 0.6f * std::tanh (z * (1.0f / 0.6f));   // voices never overpower the mix
                }
            }

            loopWrite = (loopWrite + n) & loopMask;
        }
    }

private:
    void clearLoop() noexcept
    {
        for (auto& b : loopBuf)
            std::fill (b.begin(), b.end(), 0.0f);
        loopCleared = true;
    }

    float readLoop (int ch, float pos) const noexcept
    {
        const float fl = std::floor (pos);
        const int i0 = (int) fl;
        const float frac = pos - fl;
        const auto& b = loopBuf[(size_t) ch];
        const float a = b[(size_t) (i0 & loopMask)];
        return a + frac * (b[(size_t) ((i0 + 1) & loopMask)] - a);
    }

    double sampleRate = 44100.0;
    LivePitchShifter primary, secondary;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> lagLine { 1 };
    juce::AudioBuffer<float> primBuf, secBuf;
    juce::SmoothedValue<float> onSmoothed, primLevel, secLevel, magicGain, lagSmoothed;

    std::array<std::vector<float>, 2> loopBuf;
    juce::AudioBuffer<float> inBuf;
    int loopSize = 0, loopMask = 0, loopWrite = 0;
    bool loopCleared = false;
    float loopDelayTarget = 2000.0f, resonance = 0.0f;
    swarm::OnePole loopDelay;
    std::array<float, 2> toneState {}, loopTone {};
    std::array<swarm::DCBlocker, 2> dc;

    float pitch = 7.0f, tracking = 0.8f, toneCoeff = 0.0f;
    float lastPrimRatio = 1.0f, lastSecRatio = 1.0f;
};
