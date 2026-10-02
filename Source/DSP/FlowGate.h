#pragma once

#include "DSPUtils.h"

#include <optional>

/**
 * WINGS: a pattern gate / tremolo.
 *
 * One cycle (SPEED in Hz, or DIV of the host tempo when synced) is cut into 1 .. 16 steps, each
 * with its own level - a pattern tremolo. The default pattern (2 steps: on, off) is the plain
 * square / sine of the old gate.
 *
 *  Hard   : every step holds its level, ~1.5 ms raised-cosine edges between steps (tight but
 *           click-free stutter)
 *  Smooth : a cosine glide from each step's level to the next (a tremolo shaped by the pattern)
 *
 * Edited levels glide over ~5 ms, so painting the pattern while it plays doesn't click.
 * Runs free, or locks to the host tempo and bar position when synced.
 */
class FlowGate
{
public:
    static constexpr int kMaxSteps = swarm::StepPattern::kMaxSteps;

    /** Ready-made patterns (ParamChoices::wingFills names them in this order). */
    enum Fill : int { fillPulse = 0, fillOffbeat, fillGallop, fillTriplet, fillRampUp, fillRampDown, fillStutter, fillGlitch, numFills };
    static swarm::StepPattern makeFill (int fill) noexcept
    {
        swarm::StepPattern p;
        auto set = [&p] (int n, std::initializer_list<float> levels)
        {
            p.numSteps = n;
            int k = 0;
            for (auto l : levels) p.level[(size_t) k++ % kMaxSteps] = l;
        };
        switch (fill)
        {
            case fillOffbeat:  set (2, { 0, 1 }); break;
            case fillGallop:   set (4, { 1, 0, 1, 1 }); break;
            case fillTriplet:  set (3, { 1, 1, 0 }); break;
            case fillRampUp:   set (8, { 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1, 1 }); break;
            case fillRampDown: set (8, { 1, 1, 0.9f, 0.75f, 0.6f, 0.45f, 0.3f, 0.15f }); break;
            case fillStutter:  set (8, { 1, 0, 1, 0, 1, 1, 0, 0 }); break;
            case fillGlitch:   set (16, { 1, 0.8f, 0, 1, 0.6f, 0, 1, 0.7f, 1, 0, 0.5f, 1, 0, 1, 0.7f, 0.9f }); break;
            case fillPulse:
            default:           set (2, { 1, 0 }); break;
        }
        return p;
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        amountSmoothed.reset (sr, 0.02);
        amountSmoothed.setCurrentAndTargetValue (0.0f);
        shapeBlend.reset (sr, 0.03);
        shapeBlend.setCurrentAndTargetValue (hard ? 1.0f : 0.0f);
        levelGlide = (float) (1.0 - std::exp (-1.0 / (0.005 * sr)));
        reset();
    }

    void reset()
    {
        phase = 0.0;
        levelNow = pattern.level;
    }

    /** amount in [0, 1] */
    void setParams (float amount01, bool hardMode) noexcept
    {
        hard = hardMode;
        amountSmoothed.setTargetValue (amount01);
        shapeBlend.setTargetValue (hardMode ? 1.0f : 0.0f);
    }

    /** The step pattern (levels and length; moves are ignored). */
    void setPattern (const swarm::StepPattern& p) noexcept
    {
        pattern.numSteps = juce::jlimit (1, kMaxSteps, p.numSteps);
        for (int k = 0; k < kMaxSteps; ++k)
            pattern.level[(size_t) k] = juce::jlimit (0.0f, 1.0f, p.level[(size_t) k]);
    }

    /** Free-running rate (one pattern per cycle). */
    void setRateHz (float hz) noexcept { cyclesPerSample = hz / sampleRate; }

    /** Tempo-synced: phase is derived from the host's PPQ position when available. */
    void setSynced (double cycleLengthInBeats, double bpm, std::optional<double> ppqAtBlockStart) noexcept
    {
        const double cyclesPerBeat = 1.0 / juce::jmax (1.0e-6, cycleLengthInBeats);
        cyclesPerSample = cyclesPerBeat * bpm / 60.0 / sampleRate;

        if (ppqAtBlockStart.has_value())
        {
            const double p = *ppqAtBlockStart * cyclesPerBeat;
            phase = p - std::floor (p);
        }
    }

    bool isIdle() const noexcept { return amountSmoothed.getCurrentValue() <= 0.0f && ! amountSmoothed.isSmoothing(); }

    /** The step playing now (for the editor). */
    int getCurrentStep() const noexcept { return juce::jlimit (0, pattern.numSteps - 1, (int) (phase * pattern.numSteps)); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        const int n = pattern.numSteps;
        // edge width in steps (1.5 ms), at most 40 % of a step
        const float edge = juce::jmin (0.4f, (float) juce::jmax (1.0e-4, 0.0015 * sampleRate * cyclesPerSample * n));

        for (int i = 0; i < numSamples; ++i)
        {
            const float amount = amountSmoothed.getNextValue();
            const float blend  = shapeBlend.getNextValue();
            for (int k = 0; k < n; ++k)
                levelNow[(size_t) k] += levelGlide * (pattern.level[(size_t) k] - levelNow[(size_t) k]);

            const float pos = (float) phase * (float) n;
            const int k = juce::jmin (n - 1, (int) pos);
            const float frac = pos - (float) k;
            const float prev = levelNow[(size_t) ((k + n - 1) % n)], cur = levelNow[(size_t) k], next = levelNow[(size_t) ((k + 1) % n)];

            const float smooth = cur + (next - cur) * (0.5f - 0.5f * std::cos (swarm::kPi * frac));
            const float square = frac < edge ? prev + (cur - prev) * (0.5f - 0.5f * std::cos (swarm::kPi * frac / edge)) : cur;

            const float shape = smooth + blend * (square - smooth);
            const float gain  = 1.0f - amount * (1.0f - shape);

            for (int c = 0; c < numChannels; ++c)
                audio[c][i] *= gain;

            phase += cyclesPerSample;
            if (phase >= 1.0)
                phase -= std::floor (phase);
        }
    }

private:
    double sampleRate = 44100.0;
    double phase = 0.0, cyclesPerSample = 4.0 / 44100.0;
    bool hard = true;
    swarm::StepPattern pattern { 2, { 1, 0 }, {} };
    std::array<float, kMaxSteps> levelNow { 1, 0 };
    float levelGlide = 0.005f;
    juce::SmoothedValue<float> amountSmoothed, shapeBlend;
};
