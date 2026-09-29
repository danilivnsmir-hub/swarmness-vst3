// SWARMNESS on Daisy Seed - benchmark firmware.
//
// Guitar in -> SWARMNESS engine -> out, 48 kHz, 48-sample blocks. Every 5 s it switches to the
// next benchmark scenario and prints the real CPU load (DWT cycle counter) over USB serial:
//   screen /dev/tty.usbmodem* 115200      (macOS / Linux)   or any serial terminal on Windows
//
// The engine allocates only in prepare(); those buffers (~4 MB) go to the 64 MB SDRAM through
// the arena below. Build: see Pedal/README.md.

#include "daisy_seed.h"
#include "BenchScenarios.h"

#include <cstdlib>
#include <new>

using namespace daisy;

//==============================================================================
// Engine memory: a bump allocator in SDRAM (the engine never frees while running)
namespace
{
    constexpr size_t kArenaSize = 16u << 20;
    uint8_t DSY_SDRAM_BSS arena[kArenaSize];
    size_t arenaUsed = 0;
}

void* operator new (size_t n)
{
    n = (n + 15u) & ~size_t (15);
    if (arenaUsed + n > kArenaSize)
        std::abort();
    void* p = arena + arenaUsed;
    arenaUsed += n;
    return p;
}
void* operator new[] (size_t n) { return operator new (n); }
void operator delete (void*) noexcept {}
void operator delete[] (void*) noexcept {}
void operator delete (void*, size_t) noexcept {}
void operator delete[] (void*, size_t) noexcept {}

//==============================================================================
namespace
{
    DaisySeed hw;
    swarmness::PedalEngine* engine = nullptr;
    std::vector<swarmness::bench::Scenario>* scenarios = nullptr;

    volatile uint32_t cyclesMax = 0, cyclesSum = 0, callbacks = 0;
    float bufL[swarmness::bench::kBlock], bufR[swarmness::bench::kBlock];

    void audioCallback (AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
    {
        const uint32_t t0 = DWT->CYCCNT;

        for (size_t i = 0; i < size; ++i)
            bufL[i] = bufR[i] = in[0][i];   // mono guitar in, stereo out
        float* io[2] = { bufL, bufR };
        engine->process (io, 2, (int) size);
        for (size_t i = 0; i < size; ++i)
        {
            out[0][i] = bufL[i];
            out[1][i] = bufR[i];
        }

        const uint32_t dt = DWT->CYCCNT - t0;
        cyclesSum += dt;
        if (dt > cyclesMax)
            cyclesMax = dt;
        ++callbacks;
    }
}

int main()
{
    hw.Init (true);   // boost: 480 MHz
    hw.SetAudioBlockSize (swarmness::bench::kBlock);
    hw.SetAudioSampleRate (SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.StartLog (false);

    // cycle counter
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    scenarios = new std::vector<swarmness::bench::Scenario> (swarmness::bench::scenarios());
    engine = new swarmness::PedalEngine();
    engine->prepare (hw.AudioSampleRate(), swarmness::bench::kBlock);
    engine->setParams ((*scenarios)[0].params);

    hw.PrintLine ("SWARMNESS bench on Daisy Seed: %u KB engine memory in SDRAM", (unsigned) (arenaUsed / 1024));
    hw.StartAudio (audioCallback);

    const float budget = (float) swarmness::bench::kBlock * (float) System::GetSysClkFreq() / hw.AudioSampleRate();
    size_t current = 0;
    uint32_t lastSwitch = System::GetNow();

    while (true)
    {
        System::Delay (1000);
        const uint32_t n = callbacks, sum = cyclesSum, peak = cyclesMax;
        callbacks = 0; cyclesSum = 0; cyclesMax = 0;
        if (n > 0)
            hw.PrintLine ("%-58s avg " FLT_FMT (1) "%%  peak " FLT_FMT (1) "%%", (*scenarios)[current].name,
                          FLT_VAR (1, 100.0f * (float) sum / (float) n / budget), FLT_VAR (1, 100.0f * (float) peak / budget));

        if (System::GetNow() - lastSwitch > 5000)
        {
            lastSwitch = System::GetNow();
            current = (current + 1) % scenarios->size();
            engine->setParams ((*scenarios)[current].params);
            hw.PrintLine ("---- %s", (*scenarios)[current].name);
        }
    }
}
