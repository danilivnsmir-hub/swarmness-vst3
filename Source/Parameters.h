#pragma once

#include <JuceHeader.h>

/**
 * Central definition of every automatable parameter.
 *
 * Signal flow:  [SMOKE pre] -> STING -> HIVE -> SWARM -> [SMOKE post] -> MIX -> WINGS -> OUTPUT
 * (internal IDs keep the original DSP names: noise = STING, rainbow/rb = HIVE, fuzz = SMOKE, flow = WINGS)
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
    inline constexpr const char* fuzzPost    = "fuzzPost";     // false = before the pitch stages
    inline constexpr const char* fuzz        = "fuzz";
    inline constexpr const char* fuzzTone    = "fuzzTone";
    inline constexpr const char* fuzzGate    = "fuzzGate";
    inline constexpr const char* fuzzVoice   = "fuzzVoice";    // DOWN (doom low-mids) / MID / UP (screaming upper mids)
    inline constexpr const char* fuzzScoop   = "fuzzScoop";    // mid scoop depth
    inline constexpr const char* fuzzGlare   = "fuzzGlare";    // gated octave-up
    inline constexpr const char* fuzzBlend   = "fuzzBlend";    // clean signal under the fuzz

    // WINGS (rhythmic gate)
    inline constexpr const char* flowOn      = "flowOn";
    inline constexpr const char* flowHard    = "flowHard";
    inline constexpr const char* flowSync    = "flowSync";
    inline constexpr const char* flowAmount  = "flowAmount";
    inline constexpr const char* flowSpeed   = "flowSpeed";
    inline constexpr const char* flowDiv     = "flowDiv";

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

/** Performance switches: not stored in presets (like a pedal's footswitch position). */
inline bool isPerformanceParameter (const juce::String& id)
{
    return id == ParamIDs::bypass || id == ParamIDs::oct1 || id == ParamIDs::oct2 || id == ParamIDs::magicHold;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
