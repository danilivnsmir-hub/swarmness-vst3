#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>

/**
 * Central definition of every automatable parameter.
 *
 * Signal flow: INPUT -> a reorderable chain of blocks (see Chain below) -> OUTPUT.
 * PITCH = STING (footswitch octaves) in parallel with HIVE (harmony voices).
 * (internal IDs keep the original DSP names: noise = STING, rainbow/rb = HIVE, fuzz = SMOKE, flow = WINGS,
 *  geq = COMB graphic EQ, peq = CARVE parametric EQ, rev = CRYPT reverb)
 */
namespace ParamIDs
{
    // STING: momentary octave footswitches + mangling (ANGER = panic, FRENZY = chaos, BUZZ = speed, DIVE = down)
    inline constexpr const char* oct1        = "oct1";         // footswitch: +1 octave
    inline constexpr const char* oct2        = "oct2";         // footswitch: +2 octaves
    inline constexpr const char* noiseDown   = "noiseDown";    // footswitches shift down instead of up
    inline constexpr const char* rise        = "rise";         // glide into the interval (footswitch down)
    inline constexpr const char* fall        = "fall";         // glide back home (footswitch released)
    inline constexpr const char* panic       = "panic";
    inline constexpr const char* chaos       = "chaos";
    inline constexpr const char* speed       = "speed";
    inline constexpr const char* stingMix    = "stingMix";
    inline constexpr const char* stingDetune = "stingDetune";  // fine offset of the octave, cents
    inline constexpr const char* stingRaw    = "stingRaw";     // vintage lo-fi shifter engine     // dry / shifted blend while a footswitch is down

    // HIVE: harmony voices with regeneration (DRONE = primary, QUEEN = secondary, TRAILS = magic; the VENOM footswitch = magicHold)
    inline constexpr const char* rbOn        = "rbOn";
    inline constexpr const char* rbPitch     = "rbPitch";
    inline constexpr const char* rbSnap      = "rbSnap";
    inline constexpr const char* rbRaw       = "rbRaw";        // vintage lo-fi shifter engine
    inline constexpr const char* rbPrimary   = "rbPrimary";
    inline constexpr const char* rbSecondary = "rbSecondary";
    inline constexpr const char* rbTone      = "rbTone";
    inline constexpr const char* rbTracking  = "rbTracking";
    inline constexpr const char* rbDetune    = "rbDetune";     // cents: DRONE up, QUEEN down (width)
    inline constexpr const char* rbMix       = "rbMix";        // dry / HIVE voices (50% = both full, 100% = voices only)
    inline constexpr const char* rbMagic     = "rbMagic";      // TRAILS: regeneration (repeats climb / fall by PITCH)
    inline constexpr const char* rbTime      = "rbTime";       // time between repeats (free)
    inline constexpr const char* rbSync      = "rbSync";       // repeats locked to the host tempo
    inline constexpr const char* rbDiv       = "rbDiv";        // repeat time (tempo division)
    inline constexpr const char* magicHold   = "magicHold";    // footswitch: magic to self-oscillation
    inline constexpr const char* linkOct1    = "linkOct1";     // mini switch: MAGIC footswitch also engages +1 OCT
    inline constexpr const char* linkOct2    = "linkOct2";     // mini switch: MAGIC footswitch also engages +2 OCT

    // SWARM (chorus)
    inline constexpr const char* swarmOn     = "swarmOn";
    inline constexpr const char* swarmDeep   = "swarmDeep";
    inline constexpr const char* swarmRate   = "swarmRate";
    inline constexpr const char* swarmDepth  = "swarmDepth";
    inline constexpr const char* swarmMix    = "swarmMix";

    // SMOKE (fuzz)
    inline constexpr const char* fuzzOn      = "fuzzOn";
    inline constexpr const char* fuzzPostLegacy = "fuzzPost";  // removed in beta.16 (the chain order replaces it); read for migration only
    inline constexpr const char* fuzz        = "fuzz";
    inline constexpr const char* fuzzTone    = "fuzzTone";
    inline constexpr const char* fuzzGate    = "fuzzGate";
    inline constexpr const char* fuzzVoice   = "fuzzVoice";    // DOWN (doom low-mids) / MID / UP (screaming upper mids)
    inline constexpr const char* fuzzScoop   = "fuzzScoop";    // mid scoop depth
    inline constexpr const char* fuzzGlare   = "fuzzGlare";    // gated octave-up
    inline constexpr const char* fuzzBlend   = "fuzzBlend";    // clean signal under the fuzz
    inline constexpr const char* fuzzSag     = "fuzzSag";      // supply sag / bloom ("breathing")

    // WINGS (rhythmic gate)
    inline constexpr const char* flowOn      = "flowOn";
    inline constexpr const char* flowHard    = "flowHard";
    inline constexpr const char* flowSync    = "flowSync";
    inline constexpr const char* flowAmount  = "flowAmount";
    inline constexpr const char* flowSpeed   = "flowSpeed";
    inline constexpr const char* flowDiv     = "flowDiv";

    // COMB (10-band graphic EQ)
    inline constexpr const char* geqOn       = "geqOn";
    inline constexpr const char* geqLevel    = "geqLevel";
    inline constexpr const char* geqBands[]  { "geqB0", "geqB1", "geqB2", "geqB3", "geqB4",
                                               "geqB5", "geqB6", "geqB7", "geqB8", "geqB9" };

    // CARVE (parametric EQ): 24 dB/oct low / high cut, shelves and three bells
    inline constexpr const char* peqOn       = "peqOn";
    inline constexpr const char* peqHpFreq   = "peqHpFreq";
    inline constexpr const char* peqLpFreq   = "peqLpFreq";
    inline constexpr const char* peqLowFreq  = "peqLowFreq";
    inline constexpr const char* peqLowGain  = "peqLowGain";
    inline constexpr const char* peqB1Freq   = "peqB1Freq";
    inline constexpr const char* peqB1Gain   = "peqB1Gain";
    inline constexpr const char* peqB1Q      = "peqB1Q";
    inline constexpr const char* peqB2Freq   = "peqB2Freq";
    inline constexpr const char* peqB2Gain   = "peqB2Gain";
    inline constexpr const char* peqB2Q      = "peqB2Q";
    inline constexpr const char* peqB3Freq   = "peqB3Freq";
    inline constexpr const char* peqB3Gain   = "peqB3Gain";
    inline constexpr const char* peqB3Q      = "peqB3Q";
    inline constexpr const char* peqHighFreq = "peqHighFreq";
    inline constexpr const char* peqHighGain = "peqHighGain";

    // CRYPT (reverb: algorithmic FDN or a loaded impulse response)
    inline constexpr const char* revOn       = "revOn";
    inline constexpr const char* revType     = "revType";
    inline constexpr const char* revMix      = "revMix";
    inline constexpr const char* revDecay    = "revDecay";
    inline constexpr const char* revSize     = "revSize";
    inline constexpr const char* revPreDelay = "revPreDelay";
    inline constexpr const char* revTone     = "revTone";
    inline constexpr const char* revLowCut   = "revLowCut";
    inline constexpr const char* revMod      = "revMod";
    inline constexpr const char* revDuck     = "revDuck";

    // OUTPUT / GLOBAL
    inline constexpr const char* input       = "input";        // input sensitivity (compensated at the output)
    inline constexpr const char* output      = "output";
    inline constexpr const char* switchMode  = "switchMode";   // footswitch behaviour: momentary / latch
    inline constexpr const char* bypass      = "bypass";
}

namespace ParamRanges
{
    inline constexpr float outputMinDb = -24.0f;
    inline constexpr float outputMaxDb = 12.0f;
    inline constexpr float inputMinDb  = -18.0f;
    inline constexpr float inputMaxDb  = 18.0f;
}

namespace ParamChoices
{
    inline const juce::StringArray switchModes { "Momentary", "Latch" };
    inline const juce::StringArray fuzzVoices  { "Down", "Mid", "Up" };
    inline const juce::StringArray reverbTypes { "Room", "Plate", "Hall", "Abyss", "IR" };
    inline const juce::StringArray divisions   { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                                 "1/4T", "1/8T", "1/16T", "1/8D", "1/16D" };

    /** Length of each tempo division, in quarter notes. */
    inline double divisionInBeats (int index)
    {
        static constexpr double beats[] { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                          2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 0.75, 0.375 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }
}

namespace ParamRanges
{
    inline constexpr float geqBandHz[] { 31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    inline constexpr float geqMaxDb  = 12.0f;
    inline constexpr float peqMaxDb  = 18.0f;
    inline constexpr float peqHpOff  = 10.0f;      // low cut at its minimum = off
    inline constexpr float peqLpOff  = 22000.0f;   // high cut at its maximum = off
}

/**
 * The reorderable effect chain. Every block exists once, so the host always sees the same
 * parameters; each block has a (non-automatable) slot parameter and the chain is the blocks
 * sorted by slot (ties: default order). Any combination of slot values is a valid chain, and
 * blocks added in later versions just get a default slot in between.
 */
namespace Chain
{
    enum Block : int { pitch = 0, smoke, swarm, wings, comb, carve, crypt, numBlocks };

    inline constexpr const char* slotIds[numBlocks] { "chainPitch", "chainSmoke", "chainSwarm", "chainWings",
                                                      "chainComb", "chainCarve", "chainCrypt" };
    inline constexpr int defaultSlots[numBlocks] { 20, 10, 30, 40, 50, 60, 70 };   // SMOKE, PITCH, SWARM, WINGS, COMB, CARVE, CRYPT
    inline constexpr int slotMax = 99;
    inline constexpr int legacyPostSmokeSlot = 35;   // old "SMOKE POST" = after SWARM, before WINGS

    inline constexpr const char* names[numBlocks]     { "PITCH", "SMOKE", "SWARM", "WINGS", "COMB", "CARVE", "CRYPT" };
    inline constexpr const char* subtitles[numBlocks] { "STING + HIVE", "FUZZ", "CHORUS", "TREMOLO", "GRAPHIC EQ", "PARAM EQ", "REVERB" };

    using Order = std::array<int, numBlocks>;

    /** Blocks sorted by slot value (stable: ties keep the default order). */
    inline Order orderFromSlots (const std::array<float, numBlocks>& slots) noexcept
    {
        Order order;
        for (int i = 0; i < numBlocks; ++i)
            order[(size_t) i] = i;
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b)
        {
            const int sa = juce::roundToInt (slots[(size_t) a]), sb = juce::roundToInt (slots[(size_t) b]);
            return sa != sb ? sa < sb : defaultSlots[a] < defaultSlots[b];
        });
        return order;
    }

    /** Where a block sits: in the main (series) line, or on parallel path A (upper) / B (lower). */
    enum Lane : int { series = 0, pathA, pathB };
    inline constexpr const char* laneIds[numBlocks] { "lanePitch", "laneSmoke", "laneSwarm", "laneWings",
                                                      "laneComb", "laneCarve", "laneCrypt" };
    inline constexpr const char* parallelMixId = "chainParMix";   // A <-> B balance at the merge

    using Lanes = std::array<int, numBlocks>;

    /** Order + lanes: everything the audio thread needs to route the chain. */
    struct Layout
    {
        Order order {};
        Lanes lanes {};
        bool operator== (const Layout& o) const noexcept { return order == o.order && lanes == o.lanes; }
        bool operator!= (const Layout& o) const noexcept { return ! (*this == o); }
    };

    /**
     * The routing a layout means:  pre (series) -> split -> [path A || path B] -> merge -> post (series).
     * The parallel section sits where its first block is in the order; series blocks before it are
     * "pre", all other series blocks are "post". An empty path passes the dry signal (parallel blend).
     */
    struct Plan
    {
        std::array<int, numBlocks> pre {}, a {}, b {}, post {};
        int numPre = 0, numA = 0, numB = 0, numPost = 0;
        bool hasParallel() const noexcept { return numA + numB > 0; }
    };

    inline Plan planFor (const Layout& l) noexcept
    {
        Plan p;
        bool seenParallel = false;
        for (int blk : l.order)
        {
            const int lane = l.lanes[(size_t) blk];
            if (lane == pathA)      { p.a[(size_t) p.numA++] = blk; seenParallel = true; }
            else if (lane == pathB) { p.b[(size_t) p.numB++] = blk; seenParallel = true; }
            else if (seenParallel)  p.post[(size_t) p.numPost++] = blk;
            else                    p.pre[(size_t) p.numPre++] = blk;
        }
        return p;
    }

    inline Order defaultOrder() noexcept
    {
        std::array<float, numBlocks> slots;
        for (int i = 0; i < numBlocks; ++i)
            slots[(size_t) i] = (float) defaultSlots[i];
        return orderFromSlots (slots);
    }

    /** Slot values that realise an order (10, 20, 30, ... leaving room for future blocks). */
    inline std::array<float, numBlocks> slotsForOrder (const Order& order) noexcept
    {
        std::array<float, numBlocks> slots {};
        for (int pos = 0; pos < numBlocks; ++pos)
            slots[(size_t) order[(size_t) pos]] = (float) (10 * (pos + 1));
        return slots;
    }
}

/** Performance switches: not stored in presets (like a pedal's footswitch position). */
inline bool isPerformanceParameter (const juce::String& id)
{
    return id == ParamIDs::bypass || id == ParamIDs::oct1 || id == ParamIDs::oct2 || id == ParamIDs::magicHold;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
