#pragma once

/**
 * SWARMNESS pedal engine: the "destructive pitch-delay" pedal.
 *
 *     IN -> INPUT -> HIVE (SHIFT + VOICES + TRAILS + MANGLE) -> SWARM (chorus) -> VOLUME -> OUT
 *
 * Pure C++17: compiles with the plug-in (JUCE) or stand-alone with SWARM_NO_JUCE for pedal
 * hardware (Daisy / STM32H7, Raspberry Pi / Linux ARM, ...). It uses the exact same DSP classes
 * as the plug-in, so the pedal and the plug-in sound identical.
 *
 * Real-time rules: prepare() allocates; setParams() / process() never allocate or lock.
 */

#include "../DSP/HiveBlock.h"
#include "../DSP/SwarmChorus.h"

namespace swarmness
{
    struct PedalParams
    {
        // Footswitches (momentary or latched by the pedal's firmware)
        bool shiftA = false, shiftB = false, venom = false, bypass = false;
        bool linkA = false, linkB = false;   // VENOM drags the SHIFTs in

        HiveBlock::Settings hive;                  // see HiveBlock.h

        // SWARM
        bool swarmOn = false, swarmDeep = false;
        float swarmRateHz = 0.6f, swarmDepth = 0.5f, swarmMix = 0.5f;   // 0..1

        float inputDb = 0.0f, outputDb = 0.0f;
    };

    class PedalEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize)
        {
            sr = sampleRate;
            maxBlock = sw::jlimit (1, kTrack, maxBlockSize);
            hive.prepare (sr, maxBlock);
            swarm.prepare (sr);
            dry.setSize (2, maxBlock, false, false, true);
            inGain.reset (sr, 0.03);
            outGain.reset (sr, 0.03);
            bypassMix.reset (sr, 0.02);
            inGain.setCurrentAndTargetValue (dbToGain (params.inputDb));
            outGain.setCurrentAndTargetValue (dbToGain (params.outputDb));
            bypassMix.setCurrentAndTargetValue (params.bypass ? 1.0f : 0.0f);
        }

        void reset()
        {
            hive.reset();
            swarm.reset();
        }

        void setParams (const PedalParams& p) noexcept { params = p; }

        /** Live SHIFT transposition (for an LED / display). */
        float getShiftSemitones() const noexcept { return hive.getShiftSemitones(); }

        /**
         * In-place processing. numChannels = 1 (mono pedal) or 2 (stereo out: pass the guitar in
         * both channels). Blocks longer than maxBlockSize are split internally.
         */
        void process (float* const* io, int numChannels, int numSamples) noexcept
        {
            numChannels = sw::jlimit (1, 2, numChannels);
            for (int start = 0; start < numSamples; start += maxBlock)
            {
                const int n = sw::jmin (maxBlock, numSamples - start);
                float* sub[2] = { io[0] + start, io[numChannels - 1] + start };
                processChunk (sub, numChannels, n);
            }
        }

    private:
        static float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

        void processChunk (float* const* audio, int numChannels, int n) noexcept
        {
            const auto& p = params;
            for (int ch = 0; ch < numChannels; ++ch)
                dry.copyFrom (ch, 0, audio[ch], n);

            // INPUT sensitivity (undone at the output)
            inGain.setTargetValue (dbToGain (p.inputDb));
            for (int i = 0; i < n; ++i)
            {
                const float g = inGain.getNextValue();
                inTrack[(size_t) i] = g;
                for (int ch = 0; ch < numChannels; ++ch)
                    audio[ch][i] *= g;
            }

            // Footswitches work even while bypassed, like momentary pedals
            auto s = p.hive;
            s.shiftA = p.shiftA || (p.venom && p.linkA);
            s.shiftB = p.shiftB || (p.venom && p.linkB);
            s.venom = p.venom;
            hive.setParams (s);
            hive.process (audio, numChannels, n);

            swarm.setParams (p.swarmRateHz, p.swarmDepth, p.swarmOn ? p.swarmMix : 0.0f, p.swarmDeep);
            swarm.process (audio, numChannels, n);

            const bool anyHeld = s.shiftA || s.shiftB || s.venom;
            bypassMix.setTargetValue (p.bypass && ! anyHeld && ! hive.isShiftEngaged() ? 1.0f : 0.0f);
            outGain.setTargetValue (dbToGain (p.outputDb));

            for (int i = 0; i < n; ++i)
            {
                const float g = outGain.getNextValue() / inTrack[(size_t) i];
                const float b = bypassMix.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float y = audio[ch][i] * g;
                    y += b * (dry.getSample (ch, i) - y);
                    // never let a NaN out; soft limit above +6 dBFS
                    if (! std::isfinite (y))           y = 0.0f;
                    else if (std::abs (y) > 2.0f)      y = 2.0f * std::tanh (y * 0.5f);
                    audio[ch][i] = y;
                }
            }
        }

        static constexpr int kTrack = 4096;
        double sr = 48000.0;
        int maxBlock = 64;
        PedalParams params;
        HiveBlock hive;
        SwarmChorus swarm;
        sw::AudioBuffer<float> dry;
        std::array<float, kTrack> inTrack {};
        sw::SmoothedValue<float> inGain, outGain, bypassMix;
    };
}
