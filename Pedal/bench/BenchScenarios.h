#pragma once

// Test signal and scenarios shared by the pedal benchmark (no JUCE) and the plug-in test suite,
// so both can render exactly the same thing and be compared sample by sample.

#include "Engine/SwarmnessPedalEngine.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace swarmness::bench
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 48;   // 1 ms - typical for a pedal

    /** Guitar-ish test signal: decaying harmonic plucks (same as the plug-in tests). */
    inline std::vector<float> makeGuitar (int numSamples)
    {
        std::vector<float> b ((size_t) numSamples);
        uint32_t rng = 42;
        const float notes[] = { 82.41f, 110.0f, 146.83f, 196.0f };
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = i / kRate;
            const int note = (int) (t / 0.5) % 4;
            const double tn = std::fmod (t, 0.5);
            const double env = std::exp (-tn * 4.0);
            double s = 0.0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (2.0 * 3.14159265358979 * notes[note] * h * tn) / (double) h;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            s = 0.3 * env * s + 0.002 * ((double) (rng >> 8) / 16777216.0 - 0.5);
            b[(size_t) i] = (float) s;
        }
        return b;
    }

    struct Scenario
    {
        const char* name;
        swarmness::PedalParams params;
    };

    inline std::vector<Scenario> scenarios()
    {
        using P = swarmness::PedalParams;
        std::vector<Scenario> list;

        P idle;
        list.push_back ({ "idle (nothing engaged)", idle });

        P oct = idle;
        oct.oct1 = true;
        list.push_back ({ "STING +1 OCT (RAW)", oct });

        P angry = idle;
        angry.oct2 = true;
        angry.pitch.anger = 0.8f; angry.pitch.frenzy = 0.5f; angry.pitch.buzz = 0.5f;
        list.push_back ({ "STING +2 OCT + ANGER/FRENZY/BUZZ", angry });

        P hive = idle;
        hive.pitch.hiveOn = true; hive.pitch.drone = 0.6f; hive.pitch.queen = 0.4f; hive.pitch.trails = 0.6f;
        list.push_back ({ "HIVE (RAW) DRONE+QUEEN, TRAILS 60%", hive });

        P hiveClean = hive;
        hiveClean.pitch.hiveRaw = false;
        list.push_back ({ "HIVE (clean engine) DRONE+QUEEN, TRAILS", hiveClean });

        P swarm = idle;
        swarm.swarmOn = true; swarm.swarmDeep = true; swarm.swarmDepth = 0.8f;
        list.push_back ({ "SWARM DEEP only", swarm });

        P worst = angry;
        worst.pitch.hiveOn = true; worst.pitch.drone = 0.6f; worst.pitch.queen = 0.5f; worst.pitch.trails = 0.8f;
        worst.venom = true;
        worst.swarmOn = true; worst.swarmDeep = true; worst.swarmDepth = 0.8f;
        list.push_back ({ "WORST CASE: STING +2 mangled + HIVE + VENOM + SWARM DEEP", worst });
        return list;
    }


    /** Renders a scenario (stereo interleaved float32). */
    inline std::vector<float> render (const Scenario& sc, int seconds)
    {
        swarmness::PedalEngine engine;
        engine.prepare (kRate, kBlock);
        engine.setParams (sc.params);
        const int total = (int) (kRate * seconds);
        const auto input = makeGuitar (total);
        std::vector<float> out ((size_t) total * 2), l ((size_t) kBlock), r ((size_t) kBlock);
        for (int start = 0; start < total; start += kBlock)
        {
            const int n = total - start < kBlock ? total - start : kBlock;
            for (int i = 0; i < n; ++i)
                l[(size_t) i] = r[(size_t) i] = input[(size_t) (start + i)];
            float* io[2] = { l.data(), r.data() };
            engine.process (io, 2, n);
            for (int i = 0; i < n; ++i)
            {
                out[(size_t) (start + i) * 2] = l[(size_t) i];
                out[(size_t) (start + i) * 2 + 1] = r[(size_t) i];
            }
        }
        return out;
    }
}
