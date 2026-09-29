// SWARMNESS pedal engine benchmark - no JUCE, plain C++17.
//
//   pedal_bench                 CPU load per scenario (48 kHz, 48-sample blocks), memory use
//   pedal_bench --render f.raw  renders the reference scenario to raw float32 (stereo interleaved)
//   pedal_bench --count         one pass per scenario only (for instruction counting / emulators)
//
// Build (host / Raspberry Pi):  see Pedal/bench/CMakeLists.txt or Pedal/README.md

#include "Engine/SwarmnessPedalEngine.h"
#include "BenchScenarios.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

//==============================================================================
// Count heap use (prepare() is the only place the engine allocates)
static size_t gAllocated = 0;
void* operator new (size_t n)
{
    gAllocated += n;
    if (void* p = std::malloc (n))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }

//==============================================================================
namespace
{
    using namespace swarmness::bench;

    double gPeakBlock = 0.0;   // worst single block of the last run, % of the block period

    double runScenario (const Scenario& sc, int numChannels, int seconds, std::vector<float>* renderOut)
    {
        gPeakBlock = 0.0;
        swarmness::PedalEngine engine;
        engine.prepare (kRate, kBlock);
        engine.setParams (sc.params);

        const int total = (int) (kRate * seconds);
        const auto input = makeGuitar (total);
        std::vector<float> l (kBlock), r (kBlock);
        if (renderOut != nullptr)
            renderOut->assign ((size_t) total * 2, 0.0f);

        const auto t0 = std::chrono::steady_clock::now();
        for (int start = 0; start < total; start += kBlock)
        {
            const int n = std::min (kBlock, total - start);
            std::copy_n (input.data() + start, n, l.data());
            std::copy_n (input.data() + start, n, r.data());
            float* io[2] = { l.data(), r.data() };
            const auto b0 = std::chrono::steady_clock::now();
            engine.process (io, numChannels, n);
            const double blockSecs = std::chrono::duration<double> (std::chrono::steady_clock::now() - b0).count();
            gPeakBlock = std::max (gPeakBlock, blockSecs / (n / kRate) * 100.0);
            if (renderOut != nullptr)
                for (int i = 0; i < n; ++i)
                {
                    (*renderOut)[(size_t) (start + i) * 2]     = l[(size_t) i];
                    (*renderOut)[(size_t) (start + i) * 2 + 1] = numChannels > 1 ? r[(size_t) i] : l[(size_t) i];
                }
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        return secs / seconds * 100.0;   // % of one core in real time
    }
}

int main (int argc, char** argv)
{
    if (argc >= 3 && std::string (argv[1]) == "--render")
    {
        const auto out = render (scenarios().back(), 3);
        if (FILE* f = std::fopen (argv[2], "wb"))
        {
            std::fwrite (out.data(), sizeof (float), out.size(), f);
            std::fclose (f);
            std::printf ("wrote %s (%zu frames, stereo float32, 48 kHz)\n", argv[2], out.size() / 2);
            return 0;
        }
        return 1;
    }

    if (argc >= 5 && std::string (argv[1]) == "--one")
    {
        // one scenario, e.g. for instruction counting: --one <index> <channels> <seconds>
        const auto list = scenarios();
        const auto& sc = list[(size_t) std::atoi (argv[2]) % list.size()];
        const double load = runScenario (sc, std::atoi (argv[3]), std::atoi (argv[4]), nullptr);
        std::printf ("%s: %.2f%% avg, worst block %.1f%%\n", sc.name, load, gPeakBlock);
        return 0;
    }

    const bool countOnly = argc >= 2 && std::string (argv[1]) == "--count";
    const int seconds = countOnly ? 1 : 10;

    {
        const size_t before = gAllocated;
        swarmness::PedalEngine engine;
        engine.prepare (kRate, kBlock);
        std::printf ("Engine memory at 48 kHz: %.1f KB (heap) + %.1f KB (object)\n",
                     (gAllocated - before) / 1024.0, sizeof (swarmness::PedalEngine) / 1024.0);
    }

    std::printf ("SWARMNESS pedal engine, 48 kHz, %d-sample blocks, %d s of guitar per scenario\n", kBlock, seconds);
    std::printf ("%-60s %10s %10s %12s\n", "scenario (% of one core)", "mono avg", "stereo avg", "stereo peak");
    for (const auto& sc : scenarios())
    {
        const double mono = runScenario (sc, 1, seconds, nullptr);
        const double stereo = runScenario (sc, 2, seconds, nullptr);
        std::printf ("%-60s %9.2f%% %9.2f%% %11.1f%%\n", sc.name, mono, stereo, gPeakBlock);
        std::fflush (stdout);
    }
    return 0;
}
