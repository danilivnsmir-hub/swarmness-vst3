#pragma once

#include "DSPUtils.h"

/**
 * FLOW: rhythmic gate / stutter.
 *
 *  Hard   : square gate with ~1.5 ms raised-cosine edges (tight but click-free stutter).
 *  Smooth : sine-shaped tremolo.
 *
 * Runs free at a rate in Hz, or locks to the host tempo and bar position when synced.
 */
class FlowGate
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        amountSmoothed.reset (sr, 0.02);
        amountSmoothed.setCurrentAndTargetValue (0.0f);
        shapeBlend.reset (sr, 0.03);
        shapeBlend.setCurrentAndTargetValue (hard ? 1.0f : 0.0f);
        reset();
    }

    void reset() { phase = 0.0; }

    /** amount in [0, 1] */
    void setParams (float amount01, bool hardMode) noexcept
    {
        hard = hardMode;
        amountSmoothed.setTargetValue (amount01);
        shapeBlend.setTargetValue (hardMode ? 1.0f : 0.0f);
    }

    /** Free-running rate. */
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

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        const float edge = (float) juce::jmax (1.0e-4, 0.0015 * sampleRate * cyclesPerSample); // edge width in cycles

        for (int i = 0; i < numSamples; ++i)
        {
            const float amount = amountSmoothed.getNextValue();
            const float blend  = shapeBlend.getNextValue();

            const float p = (float) phase;
            const float smooth = 0.5f + 0.5f * std::cos (swarm::kTwoPi * p);   // 1 at cycle start
            const float square = squareWithEdges (p, edge);

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
    /** 50% duty square (high during the first half of the cycle) with raised-cosine edges. */
    static float squareWithEdges (float p, float edge) noexcept
    {
        edge = juce::jmin (edge, 0.2f);
        auto ramp = [edge] (float x) { return 0.5f - 0.5f * std::cos (swarm::kPi * juce::jlimit (0.0f, 1.0f, x / edge)); };

        if (p < edge)          return ramp (p);                   // rise at cycle start
        if (p < 0.5f)          return 1.0f;
        if (p < 0.5f + edge)   return 1.0f - ramp (p - 0.5f);     // fall
        return 0.0f;
    }

    double sampleRate = 44100.0;
    double phase = 0.0, cyclesPerSample = 4.0 / 44100.0;
    bool hard = true;
    juce::SmoothedValue<float> amountSmoothed, shapeBlend;
};
