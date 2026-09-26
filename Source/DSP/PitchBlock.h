#pragma once

#include "DSPUtils.h"
#include "NoiseStage.h"
#include "RainbowStage.h"

/**
 * PITCH block: STING (footswitch octaves) in parallel with HIVE (harmony voices).
 * Shared by the plug-in and the pedal engine, so both sound the same.
 *
 * HIVE harmonises the played note (not the STING octave) and its TRAILS never pick up
 * the octave. HIVE MIX uses the pedal law: 50% = dry and voices both full, 100% = voices
 * only; it only turns down the dry part, so a held STING octave still sounds on top.
 */
class PitchBlock
{
public:
    struct Settings
    {
        // footswitches (already combined with LINK by the caller)
        bool oct1 = false, oct2 = false, venom = false;

        // STING
        bool dive = false, stingRaw = true;
        float riseMs = 30.0f, fallMs = 30.0f;
        float anger = 0.0f, frenzy = 0.0f, buzz = 0.0f, stingMix = 1.0f;   // 0..1
        float stingDetuneCents = 0.0f;

        // HIVE
        bool hiveOn = false, snap = true, hiveRaw = true;
        float pitchSemis = 7.0f;
        float drone = 0.6f, queen = 0.0f, tone = 0.6f, tracking = 0.8f, trails = 0.0f, hiveMix = 0.5f;   // 0..1
        float repeatSeconds = 0.18f, hiveDetuneCents = 0.0f;
    };

    void prepare (double sampleRate, int maxBlockSize)
    {
        noise.prepare (sampleRate, maxBlockSize);
        rainbow.prepare (sampleRate, maxBlockSize);
        hiveBuffer.setSize (2, maxBlockSize, false, false, true);
        hiveVoiceGain.reset (sampleRate, 0.02);
        hiveVoiceGain.setCurrentAndTargetValue (1.0f);
    }

    void reset()
    {
        noise.reset();
        rainbow.reset();
    }

    void setParams (const Settings& s) noexcept { settings = s; }

    bool isStingEngaged() const noexcept     { return noise.isEngaged(); }
    float getStingSemitones() const noexcept { return noise.getCurrentSemitones(); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        const auto& s = settings;
        numChannels = sw::jmin (numChannels, 2);

        for (int ch = 0; ch < numChannels; ++ch)
            hiveBuffer.copyFrom (ch, 0, audio[ch], numSamples);

        const bool hiveOn = s.hiveOn || s.venom;
        noise.setDryLevel (hiveOn ? sw::jmin (1.0f, 2.0f * (1.0f - s.hiveMix)) : 1.0f);
        hiveVoiceGain.setTargetValue (hiveOn ? sw::jmin (1.0f, 2.0f * s.hiveMix) : 1.0f);

        // ---- STING
        const float interval = s.oct2 ? 24.0f : (s.oct1 ? 12.0f : 0.0f);
        noise.setParams (s.riseMs, s.fallMs, s.anger, s.frenzy, s.buzz, s.stingMix, s.stingRaw);
        noise.setInterval ((s.dive ? -1.0f : 1.0f) * interval);
        noise.setDetuneCents (s.stingDetuneCents);
        noise.process (audio, numChannels, numSamples);

        // ---- HIVE (voices only, added on top)
        rainbow.setParams (hiveOn, s.snap ? std::round (s.pitchSemis) : s.pitchSemis, s.drone, s.queen, s.tone,
                           s.tracking, s.trails, s.repeatSeconds, s.venom, s.hiveRaw, s.hiveDetuneCents);
        float* hive[2] = { hiveBuffer.getWritePointer (0), hiveBuffer.getWritePointer (1) };
        rainbow.process (hive, numChannels, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = hiveVoiceGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                audio[ch][i] += g * hive[ch][i];
        }
    }

private:
    Settings settings;
    NoiseStage noise;
    RainbowStage rainbow;
    sw::AudioBuffer<float> hiveBuffer;
    sw::SmoothedValue<float> hiveVoiceGain;
};
