#pragma once

#include "DSPUtils.h"
#include <array>

/**
 * Tube-style drive running at 4x oversampling (linear-phase FIR half-band filters).
 *
 * An asymmetric tanh curve generates musical even + odd harmonics; a DC blocker removes
 * the bias offset, a drive-dependent low-pass tames fizz, and automatic gain compensation
 * keeps perceived loudness roughly constant while turning the knob.
 * The oversampler always runs so the reported latency never changes.
 */
class DriveStage
{
public:
    static constexpr int kMaxChannels = 2;

    DriveStage()
        : oversampler (kMaxChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true)
    {
    }

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        oversampler.initProcessing ((size_t) maxBlockSize);
        oversampler.reset();

        // Latency-matched clean path: at 0% drive the stage is bit-transparent.
        cleanDelay.setMaximumDelayInSamples (getLatencySamples() + 4);
        cleanDelay.prepare ({ sr, (juce::uint32) maxBlockSize, (juce::uint32) kMaxChannels });
        cleanDelay.setDelay ((float) getLatencySamples());
        cleanCopy.setSize (kMaxChannels, maxBlockSize, false, false, true);
        osMix.reset (sr, 0.02);
        osMix.setCurrentAndTargetValue (targetDrive > 0.0f ? 1.0f : 0.0f);

        const double osRate = sr * 4.0;
        driveSmoothed.reset (osRate, 0.03);
        driveSmoothed.setCurrentAndTargetValue (targetDrive);

        for (auto& dc : dcBlockers)
            dc.prepare (sr);

        for (auto& f : fizzFilters)
        {
            f.setType (swarm::SVF::Type::lowPass);
            f.reset();
        }
        fizzCutoff.setTime (sr / 32.0, 0.05);
        fizzCutoff.reset (cutoffForDrive (targetDrive));
        fizzMix.setTime (sr / 32.0, 0.03);
        fizzMix.reset (juce::jmin (1.0f, targetDrive * 10.0f));
        counter = 0;
    }

    void reset()
    {
        oversampler.reset();
        cleanDelay.reset();
        for (auto& dc : dcBlockers) dc.reset();
        for (auto& f : fizzFilters) f.reset();
    }

    int getLatencySamples() const { return (int) std::round (oversampler.getLatencyInSamples()); }

    /** amount in [0, 1] */
    void setDrive (float amount) noexcept
    {
        targetDrive = juce::jlimit (0.0f, 1.0f, amount);
        driveSmoothed.setTargetValue (targetDrive);
        osMix.setTargetValue (targetDrive > 0.0f ? 1.0f : 0.0f);
    }

    void process (juce::dsp::AudioBlock<float> block) noexcept
    {
        const int numChannels = (int) juce::jmin ((size_t) kMaxChannels, block.getNumChannels());
        auto sub = block.getSubsetChannelBlock (0, (size_t) numChannels);
        const int numSamples = (int) sub.getNumSamples();

        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = sub.getChannelPointer ((size_t) c);
            float* dst = cleanCopy.getWritePointer (c);
            for (int i = 0; i < numSamples; ++i)
            {
                cleanDelay.pushSample (c, src[i]);
                dst[i] = cleanDelay.popSample (c);
            }
        }

        auto up = oversampler.processSamplesUp (sub);
        const int upSamples = (int) up.getNumSamples();

        for (int i = 0; i < upSamples; ++i)
        {
            const float d = driveSmoothed.getNextValue();

            if (d < 1.0e-4f)
                continue; // clean: oversampling round-trip only (transparent, keeps latency)

            if (! juce::exactlyEqual (d, cachedDrive))
            {
                cachedDrive = d;
                // Up to +36 dB of drive, bias adds 2nd-harmonic "tube" asymmetry.
                preGain = juce::Decibels::decibelsToGain (d * 36.0f);
                bias    = 0.22f * d;
                biasOut = std::tanh (bias);
                // Saturating make-up curve: the tanh output level stops growing once the stage clips.
                makeup  = juce::Decibels::decibelsToGain (-18.0f * (1.0f - std::exp (-d * 36.0f / 14.0f)));
            }

            for (int c = 0; c < numChannels; ++c)
            {
                float* data = up.getChannelPointer ((size_t) c);
                const float x   = data[i];
                const float wet = (std::tanh (x * preGain + bias) - biasOut) * makeup;
                // Blend in over the first few percent so engaging drive never jumps.
                const float blend = juce::jmin (1.0f, d * 25.0f);
                data[i] = x + blend * (wet - x);
            }
        }

        oversampler.processSamplesDown (sub);

        // Base-rate post-processing: DC removal and drive-dependent de-fizz.
        for (int i = 0; i < numSamples; ++i)
        {
            const float mixNow = osMix.getNextValue();

            if ((counter++ & 31) == 0)
            {
                const float fc = fizzCutoff.process (cutoffForDrive (targetDrive));
                for (auto& f : fizzFilters)
                    f.setParams (sampleRate, fc, 0.6f);
                fizzMix.process (juce::jmin (1.0f, targetDrive * 10.0f));
            }

            for (int c = 0; c < numChannels; ++c)
            {
                float* data = sub.getChannelPointer ((size_t) c);
                // Both stages fade out completely when clean, keeping the path bit-transparent.
                const float x  = data[i];
                const float y  = dcBlockers[(size_t) c].process (x);
                const float lp = fizzFilters[(size_t) c].process (y);
                const float processed = x + fizzMix.get() * (lp - x);
                const float clean = cleanCopy.getSample (c, i);
                data[i] = clean + mixNow * (processed - clean);
            }
        }
    }

private:
    float cutoffForDrive (float d) const noexcept
    {
        const float nyq = (float) sampleRate * 0.45f;
        return juce::jmin (nyq, 22000.0f * std::pow (0.4f, d)); // 22k (clean) -> ~8.8k (max)
    }

    juce::dsp::Oversampling<float> oversampler;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> cleanDelay { 1 };
    juce::AudioBuffer<float> cleanCopy;
    juce::SmoothedValue<float> osMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmoothed;
    std::array<swarm::DCBlocker, kMaxChannels> dcBlockers;
    std::array<swarm::SVF, kMaxChannels> fizzFilters;
    swarm::OnePole fizzCutoff, fizzMix;

    double sampleRate = 44100.0;
    float targetDrive = 0.0f;
    float cachedDrive = -1.0f, preGain = 1.0f, bias = 0.0f, biasOut = 0.0f, makeup = 1.0f;
    unsigned int counter = 0;
};
