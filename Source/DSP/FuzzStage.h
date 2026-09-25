#pragma once

#include "DSPUtils.h"
#include <array>

/**
 * FUZZ: two-stage silicon-style fuzz at 4x oversampling (linear-phase FIR half-band filters).
 *
 *  FUZZ : +6 .. +54 dB into an asymmetric clipper followed by a hard-knee squarer
 *  TONE : Big-Muff-style tone stack (dark low-pass <-> scooped <-> bright high-pass)
 *  GATE : starves the fuzz - bias shift plus an envelope gate that makes decays sputter
 *         and break up ("dying battery" / velcro)
 *
 * The oversampler always runs and the stage crossfades to a latency-matched clean path when
 * off, so the reported latency never changes and "off" is bit-transparent.
 */
class FuzzStage
{
public:
    static constexpr int kMaxChannels = 2;

    FuzzStage()
        : oversampler (kMaxChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true)
    {
    }

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        oversampler.initProcessing ((size_t) maxBlockSize);
        oversampler.reset();

        cleanDelay.setMaximumDelayInSamples (getLatencySamples() + 4);
        cleanDelay.prepare ({ sr, (juce::uint32) maxBlockSize, (juce::uint32) kMaxChannels });
        cleanDelay.setDelay ((float) getLatencySamples());
        cleanCopy.setSize (kMaxChannels, maxBlockSize, false, false, true);

        onMix.reset (sr, 0.02);
        onMix.setCurrentAndTargetValue (isOn ? 1.0f : 0.0f);

        const double osRate = sr * 4.0;
        gainSmoothed.reset (osRate, 0.03);
        gateSmoothed.reset (osRate, 0.03);
        gainSmoothed.setCurrentAndTargetValue (targetFuzz);
        gateSmoothed.setCurrentAndTargetValue (targetGate);
        envAttack  = (float) (1.0 - std::exp (-1.0 / (0.0007 * osRate)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.04 * osRate)));

        lpCoeff = (float) std::exp (-swarm::kTwoPi * 650.0 / sr);
        hpCoeff = (float) std::exp (-swarm::kTwoPi * 1300.0 / sr);
        toneSmoothed.reset (sr, 0.03);
        toneSmoothed.setCurrentAndTargetValue (targetTone);

        for (auto& d : dc) d.prepare (sr);
        for (auto& d : preDc) d.prepare (osRate);
        reset();
    }

    void reset()
    {
        oversampler.reset();
        cleanDelay.reset();
        for (auto& d : dc) d.reset();
        for (auto& d : preDc) d.reset();
        env = { 0.0f, 0.0f };
        lpState = hpState = hpIn = { 0.0f, 0.0f };
    }

    int getLatencySamples() const { return (int) std::round (oversampler.getLatencyInSamples()); }

    void setParams (bool on, float fuzz01, float tone01, float gate01) noexcept
    {
        isOn = on;
        targetFuzz = fuzz01;
        targetTone = tone01;
        targetGate = gate01;
        onMix.setTargetValue (on ? 1.0f : 0.0f);
        gainSmoothed.setTargetValue (fuzz01);
        gateSmoothed.setTargetValue (gate01);
        toneSmoothed.setTargetValue (tone01);
    }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, kMaxChannels);
        juce::dsp::AudioBlock<float> sub (audio, (size_t) numChannels, (size_t) numSamples);

        for (int c = 0; c < numChannels; ++c)
        {
            float* dst = cleanCopy.getWritePointer (c);
            for (int i = 0; i < numSamples; ++i)
            {
                cleanDelay.pushSample (c, audio[c][i]);
                dst[i] = cleanDelay.popSample (c);
            }
        }

        const bool active = isOn || onMix.isSmoothing() || onMix.getCurrentValue() > 0.0f;

        auto up = oversampler.processSamplesUp (sub);

        if (active)
        {
            const int upSamples = (int) up.getNumSamples();
            for (int i = 0; i < upSamples; ++i)
            {
                const float f = gainSmoothed.getNextValue();
                const float g = gateSmoothed.getNextValue();
                const float preGain = juce::Decibels::decibelsToGain (6.0f + 48.0f * f);
                const float bias = 0.45f * g;
                const float threshold = 0.06f * g * g;

                for (int c = 0; c < numChannels; ++c)
                {
                    float* data = up.getChannelPointer ((size_t) c);
                    const float x = data[i];

                    // Envelope for the starve gate (measured before the gain stage)
                    const float a = std::abs (x);
                    auto& e = env[(size_t) c];
                    e += (a > e ? envAttack : envRelease) * (a - e);

                    // Stage 1: asymmetric clipper (+ starve bias)
                    const float v = x * preGain + bias;
                    float y = v >= 0.0f ? std::tanh (v) : 1.3f * std::tanh (0.65f * v);

                    // Stage 2: hard-knee squarer
                    y = juce::jlimit (-1.0f, 1.0f, y * 1.7f);
                    y = 1.5f * y - 0.5f * y * y * y;

                    // Starve gate: sputters as the note decays
                    if (threshold > 0.0f)
                    {
                        const float open = juce::jlimit (0.0f, 1.0f, (e - 0.5f * threshold) / (0.5f * threshold));
                        y *= open * open;
                    }

                    data[i] = preDc[(size_t) c].process (y) * 0.32f;
                }
            }
        }

        oversampler.processSamplesDown (sub);

        for (int i = 0; i < numSamples; ++i)
        {
            const float mixNow = onMix.getNextValue();
            const float t = toneSmoothed.getNextValue();

            for (int c = 0; c < numChannels; ++c)
            {
                const float clean = cleanCopy.getSample (c, i);
                if (mixNow <= 0.0f)
                {
                    audio[c][i] = clean;
                    continue;
                }

                const float x = dc[(size_t) c].process (audio[c][i]);

                // Muff-style tone: dark LP and bright HP blended, scooped in the middle
                auto& lp = lpState[(size_t) c];
                lp = x + lpCoeff * (lp - x);
                auto& hp = hpState[(size_t) c];
                auto& hin = hpIn[(size_t) c];
                hp = hpCoeff * (hp + x - hin);
                hin = x;
                const float toned = (1.0f - t) * lp * 1.2f + t * hp * 1.6f + 0.25f * x;

                audio[c][i] = clean + mixNow * (toned - clean);
            }
        }
    }

private:
    juce::dsp::Oversampling<float> oversampler;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> cleanDelay { 1 };
    juce::AudioBuffer<float> cleanCopy;
    juce::SmoothedValue<float> onMix, toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed, gateSmoothed;
    std::array<swarm::DCBlocker, kMaxChannels> dc, preDc;
    std::array<float, kMaxChannels> env {}, lpState {}, hpState {}, hpIn {};

    double sampleRate = 44100.0;
    bool isOn = false;
    float targetFuzz = 0.6f, targetTone = 0.5f, targetGate = 0.0f;
    float envAttack = 0.1f, envRelease = 0.001f, lpCoeff = 0.9f, hpCoeff = 0.8f;
};
