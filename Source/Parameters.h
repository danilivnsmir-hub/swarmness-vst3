#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>

/**
 * Central definition of every automatable parameter.
 *
 * Signal flow: INPUT -> a reorderable chain of blocks (see Chain below) -> OUTPUT.
 * HIVE = SHIFT (footswitches) + VOICES + TRAILS + MANGLE, see DSP/HiveBlock.h.
 * (internal IDs keep older names: rb = HIVE, fuzz = SMOKE, flow = WINGS,
 *  geq = COMB graphic EQ, peq = CARVE parametric EQ, rev = CRYPT reverb)
 */
namespace ParamIDs
{
    // SHIFT - the footswitch shifter (IDs keep their original names so sessions / MIDI stay valid)
    // footswitches SHIFT A / SHIFT B (oct1 / oct2) transpose the signal while held
    inline constexpr const char* oct1        = "oct1";         // footswitch: SHIFT A
    inline constexpr const char* oct2        = "oct2";         // footswitch: SHIFT B
    inline constexpr const char* shiftA      = "shiftA";       // SHIFT A interval, semitones
    inline constexpr const char* shiftB      = "shiftB";       // SHIFT B interval, semitones
    inline constexpr const char* rise        = "rise";         // glide into the interval (footswitch down)
    inline constexpr const char* fall        = "fall";         // glide back home (footswitch released)
    inline constexpr const char* shOn        = "shOn";         // SHIFT A engaged permanently (power button)
    inline constexpr const char* stingMix    = "stingMix";     // SHIFT MIX: dry / shifted while engaged
    inline constexpr const char* shStack     = "shStack";      // A + B held: both intervals sound (off = B wins)
    inline constexpr const char* panic       = "panic";        // SHIFT ANGER
    inline constexpr const char* chaos       = "chaos";        // SHIFT FRENZY
    inline constexpr const char* speed       = "speed";        // SHIFT BUZZ
    inline constexpr const char* shSnap      = "shSnap";       // FRENZY jumps on 4ths / 5ths / octaves
    inline constexpr const char* shRaw       = "shRaw";        // RAW engine character
    inline constexpr const char* shDetune    = "shDetune";     // cents

    // HIVE - voices + trails
    // VOICES
    inline constexpr const char* rbOn        = "rbOn";         // voices on
    inline constexpr const char* rbPitch     = "rbPitch";      // DRONE interval
    inline constexpr const char* rbSnap      = "rbSnap";
    inline constexpr const char* rbPrimary   = "rbPrimary";    // DRONE level
    inline constexpr const char* rbSecondary = "rbSecondary";  // QUEEN level
    inline constexpr const char* rbTracking  = "rbTracking";
    // TRAILS
    inline constexpr const char* rbMagic     = "rbMagic";      // TRAILS: regeneration
    inline constexpr const char* trSteps     = "trSteps";      // TRAILS step pattern length (1..16)
    inline constexpr const char* trChop      = "trChop";       // GATE: chops every step (0 = full repeats)
    inline constexpr const char* trDry       = "trDry";        // the repeats start from the input (a delay)
    inline constexpr const char* trLevels[]  { "trL1", "trL2", "trL3", "trL4", "trL5", "trL6", "trL7", "trL8",
                                               "trL9", "trL10", "trL11", "trL12", "trL13", "trL14", "trL15", "trL16" };
    inline constexpr const char* trMoves[]   { "trM1", "trM2", "trM3", "trM4", "trM5", "trM6", "trM7", "trM8",
                                               "trM9", "trM10", "trM11", "trM12", "trM13", "trM14", "trM15", "trM16" };
    inline constexpr const char* rbTime      = "rbTime";       // time between repeats (free)
    inline constexpr const char* rbSync      = "rbSync";       // repeats locked to the host tempo
    inline constexpr const char* rbDiv       = "rbDiv";        // repeat time (tempo division)
    inline constexpr const char* rbTone      = "rbTone";
    inline constexpr const char* magicHold   = "magicHold";    // footswitch: VENOM (self-oscillation)
    inline constexpr const char* linkOct1    = "linkOct1";     // VENOM also engages SHIFT A
    inline constexpr const char* linkOct2    = "linkOct2";     // VENOM also engages SHIFT B
    // MANGLE (on the voices and trails)
    inline constexpr const char* hvMangle    = "hvMangle";     // one knob: ANGER -> FRENZY -> BUZZ
    inline constexpr const char* rbRaw       = "rbRaw";        // RAW engine character
    inline constexpr const char* rbDetune    = "rbDetune";     // cents: fine offset / spread
    inline constexpr const char* rbMix       = "rbMix";        // dry / effect (50% = both full, 100% = effect only)

    // Removed when STING and HIVE became one block (read for migration only)
    inline constexpr const char* noiseDownLegacy   = "noiseDown";
    inline constexpr const char* stingRawLegacy    = "stingRaw";
    inline constexpr const char* stingDetuneLegacy = "stingDetune";
    // Beta.20-22: one PATTERN choice instead of the step pattern (values = HiveBlock fills 0..4)
    inline constexpr const char* hivePatternLegacy = "hivePattern";
    // Beta.20-24: FOLLOW (the chain order decides now: HIVE after SHIFT follows the shifted note)
    inline constexpr const char* hiveFollowLegacy  = "hiveFollow";
    // Beta.23-25: GATE as the part of each step that sounds (100 = open); now trChop (0 = open)
    inline constexpr const char* trGateLegacy      = "trGate";
    // Beta.25-27: HIVE had its own ANGER / FRENZY / BUZZ (now the single MANGLE knob)
    inline constexpr const char* hvAngerLegacy     = "hvAnger";
    inline constexpr const char* hvFrenzyLegacy    = "hvFrenzy";
    inline constexpr const char* hvBuzzLegacy      = "hvBuzz";

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

    // HONEY (sustainer / compressor)
    inline constexpr const char* hnOn        = "hnOn";
    inline constexpr const char* hnSustain   = "hnSustain";    // threshold down + ratio up, one knob
    inline constexpr const char* hnAttack    = "hnAttack";
    inline constexpr const char* hnBlend     = "hnBlend";      // parallel blend
    inline constexpr const char* hnLevel     = "hnLevel";      // dB on top of the make-up
    inline constexpr const char* hnLimit     = "hnLimit";      // peak limiter after the compressor

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

    // AMP (amp models CLEAN / CRUNCH / LEAD with CHARACTER, or a NAM capture)
    inline constexpr const char* ampOn       = "ampOn";
    inline constexpr const char* ampChannel  = "ampChannel";
    inline constexpr const char* ampGain     = "ampGain";
    inline constexpr const char* ampBass     = "ampBass";
    inline constexpr const char* ampMid      = "ampMid";
    inline constexpr const char* ampTreble   = "ampTreble";
    inline constexpr const char* ampPresence = "ampPresence";
    inline constexpr const char* ampDepth    = "ampDepth";
    inline constexpr const char* ampMaster   = "ampMaster";
    inline constexpr const char* ampGate     = "ampGate";
    inline constexpr const char* ampLevel    = "ampLevel";
    // NAM mode has its own knobs (5 = neutral: the capture's own level, flat EQ)
    inline constexpr const char* namInput    = "namInput";
    inline constexpr const char* namBass     = "namBass";
    inline constexpr const char* namMid      = "namMid";
    inline constexpr const char* namTreble   = "namTreble";
    inline constexpr const char* namPresence = "namPresence";
    inline constexpr const char* namDepth    = "namDepth";
    inline constexpr const char* namOutput   = "namOutput";
    inline constexpr const char* namLite     = "namLite";     // A2 captures: the Lite size (less CPU)

    // WASP (tight overdrive / boost, usually in front of the AMP)
    inline constexpr const char* drvOn       = "drvOn";
    inline constexpr const char* drvVolume   = "drvVolume";
    inline constexpr const char* drvDrive    = "drvDrive";
    inline constexpr const char* drvBright   = "drvBright";
    inline constexpr const char* drvAttack   = "drvAttack";
    inline constexpr const char* drvGate     = "drvGate";
    // WASP's NAM mode: a pedal capture instead of the WASP circuit (5 = the capture as it is)
    inline constexpr const char* drvNam       = "drvNam";
    inline constexpr const char* drvNamInput  = "drvNamInput";
    inline constexpr const char* drvNamOutput = "drvNamOutput";
    inline constexpr const char* drvNamLite   = "drvNamLite";

    // CAB (modelled cabinets or a loaded IR)
    inline constexpr const char* cabOn       = "cabOn";
    inline constexpr const char* cabType     = "cabType";
    inline constexpr const char* cabMic      = "cabMic";
    inline constexpr const char* cabDist     = "cabDist";
    inline constexpr const char* cabLowCut   = "cabLowCut";
    inline constexpr const char* cabHighCut  = "cabHighCut";
    inline constexpr const char* cabLevel    = "cabLevel";
    inline constexpr const char* cabIrMix    = "cabIrMix";     // two IR slots: 0 % = A only .. 100 % = B only
    inline constexpr const char* cabIrInvB   = "cabIrInvB";    // IR B polarity inverted

    // OUTPUT / GLOBAL
    inline constexpr const char* input       = "input";        // input sensitivity (compensated at the output)
    inline constexpr const char* output      = "output";
    inline constexpr const char* switchMode  = "switchMode";   // footswitch behaviour: momentary / latch
    inline constexpr const char* scene       = "scene";        // current scene A..D of the preset
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
    inline const juce::StringArray scenes      { "A", "B", "C", "D" };
    inline const juce::StringArray fuzzVoices  { "Down", "Mid", "Up" };
    inline const juce::StringArray reverbTypes { "Room", "Plate", "Hall", "Abyss", "IR" };
    inline const juce::StringArray ampChannels { "Clean", "Crunch", "Lead", "NAM" };
    inline const juce::StringArray cabTypes    { "1x12 Open", "2x12 Open", "4x12 Brit", "4x12 Modern", "IR" };
    inline const juce::StringArray stepMoves { "Hold", "Up", "Down", "Random", "Reverse" };
    /** Ready-made TRAILS step patterns, in HiveBlock::Fill order. */
    inline const juce::StringArray trailFills { "Ladder", "Bounce", "Scatter", "Reverse", "Swell",
                                                "Echo", "Stutter", "Gallop", "Offbeat", "Glitch" };

    /** "+7 5th", "-1 oct", "+2 oct", "+19 5th+oct" ... */
    inline juce::String intervalName (int st)
    {
        static const char* names[] = { "unison", "m2", "M2", "m3", "M3", "4th", "tritone", "5th", "m6", "M6", "m7", "M7" };
        const int a = std::abs (st);
        const juce::String sign = st < 0 ? "-" : "+";
        if (a == 0)
            return "0 unison";
        if (a % 12 == 0)
            return sign + juce::String (a / 12) + " oct";
        return sign + juce::String (a) + " " + names[a % 12] + (a > 12 ? "+oct" : "");
    }

    /** Short footswitch caption: "+1 OCT", "-2 OCT", "+5TH", "-4TH", "+7 ST" ... */
    inline juce::String shiftCaption (int st)
    {
        const int a = std::abs (st);
        const juce::String sign = st < 0 ? "-" : "+";
        if (a == 0) return "SHIFT";
        if (a % 12 == 0) return sign + juce::String (a / 12) + " OCT";
        if (a == 7) return sign + "5TH";
        if (a == 5) return sign + "4TH";
        return sign + juce::String (a) + " ST";
    }
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
    // (pitch = HIVE; SHIFT was split off in beta.25 and sits right before HIVE by default)
    // (AMP and CAB came in v3.1, between HIVE and SWARM - where an amp sits on a pedalboard)
    // (WASP, the overdrive, came in v3.1 too, right in front of the AMP)
    // (HONEY, the compressor, came in 1.1 and sits first by default)
    enum Block : int { pitch = 0, smoke, swarm, wings, comb, carve, crypt, shift, amp, cab, drive, honey, numBlocks };

    inline constexpr const char* slotIds[numBlocks] { "chainPitch", "chainSmoke", "chainSwarm", "chainWings",
                                                      "chainComb", "chainCarve", "chainCrypt", "chainShift",
                                                      "chainAmp", "chainCab", "chainDrive", "chainHoney" };
    // HONEY, SMOKE, SHIFT, HIVE, WASP, AMP, CAB, SWARM, WINGS, COMB, CARVE, CRYPT
    inline constexpr int defaultSlots[numBlocks] { 20, 10, 30, 40, 50, 60, 70, 15, 23, 26, 22, 8 };
    inline constexpr int slotMax = 99;
    inline constexpr int legacyPostSmokeSlot = 35;   // old "SMOKE POST" = after SWARM, before WINGS

    inline constexpr const char* names[numBlocks]     { "HIVE", "SMOKE", "SWARM", "WINGS", "COMB", "CARVE", "CRYPT", "SHIFT", "AMP", "CAB", "WASP", "HONEY" };
    inline constexpr const char* subtitles[numBlocks] { "HARMONIES", "FUZZ", "CHORUS", "TREMOLO", "GRAPHIC EQ", "PARAM EQ", "REVERB", "PITCH SHIFT",
                                                        "AMPLIFIER", "CABINET", "OVERDRIVE", "SUSTAIN" };

    using Order = std::array<int, numBlocks>;

    /** Blocks sorted by slot value (ties keep the default order). The default slots all differ, so this is a
        total order and std::sort needs no stable_sort (which allocates - this runs on the audio thread). */
    inline Order orderFromSlots (const std::array<float, numBlocks>& slots) noexcept
    {
        Order order;
        for (int i = 0; i < numBlocks; ++i)
            order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [&] (int a, int b)
        {
            const int sa = juce::roundToInt (slots[(size_t) a]), sb = juce::roundToInt (slots[(size_t) b]);
            return sa != sb ? sa < sb : defaultSlots[a] < defaultSlots[b];
        });
        return order;
    }

    /** Where a block sits: in the main (series) line, or on parallel path A (upper) / B (lower). */
    enum Lane : int { series = 0, pathA, pathB };
    inline constexpr const char* laneIds[numBlocks] { "lanePitch", "laneSmoke", "laneSwarm", "laneWings",
                                                      "laneComb", "laneCarve", "laneCrypt", "laneShift",
                                                      "laneAmp", "laneCab", "laneDrive", "laneHoney" };
    /** A / B balance of each split, numbered left to right (a fifth split shares the fourth's mix). */
    inline constexpr int maxSplits = 4;
    inline constexpr const char* parallelMixIds[maxSplits] { "chainParMix", "chainParMix2", "chainParMix3", "chainParMix4" };

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
     * The routing a layout means: a sequence of stages, each either one series block or a split
     * (path A || path B, then a merge). Parallel blocks that follow each other in the order form
     * one split; a series block between them starts a new one:
     *     FX1 -> [FX2 || FX3] -> FX4 -> [FX5 || FX6] -> FX7
     * An empty path carries the dry signal (parallel blend).
     */
    struct Stage
    {
        bool parallel = false;
        int block = -1;                      // series stage
        std::array<int, numBlocks> a {}, b {};
        int numA = 0, numB = 0;
        int split = -1;                      // index of the split (its MIX parameter)
    };

    struct Plan
    {
        std::array<Stage, numBlocks> stages {};
        int numStages = 0, numSplits = 0;
    };

    inline Plan planFor (const Layout& l) noexcept
    {
        Plan p;
        for (int blk : l.order)
        {
            const int lane = l.lanes[(size_t) blk];
            if (lane == series)
            {
                auto& st = p.stages[(size_t) p.numStages++];
                st = {};
                st.block = blk;
                continue;
            }
            if (p.numStages == 0 || ! p.stages[(size_t) p.numStages - 1].parallel)
            {
                auto& st = p.stages[(size_t) p.numStages++];
                st = {};
                st.parallel = true;
                st.split = juce::jmin (p.numSplits++, maxSplits - 1);
            }
            auto& st = p.stages[(size_t) p.numStages - 1];
            if (lane == pathA) st.a[(size_t) st.numA++] = blk;
            else               st.b[(size_t) st.numB++] = blk;
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
    return id == ParamIDs::bypass || id == ParamIDs::oct1 || id == ParamIDs::oct2 || id == ParamIDs::magicHold
        || id == ParamIDs::scene;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
