#pragma once

#include <JuceHeader.h>

/**
 * Central definition of every automatable parameter.
 *
 * All parameters use real-world units (Hz, dB, ms, semitones, %) so that
 * hosts display meaningful values and presets stay readable.
 */
namespace ParamIDs
{
    // VOLTAGE / Pitch
    inline constexpr const char* pitchOn     = "pitchOn";
    inline constexpr const char* octave      = "octave";
    inline constexpr const char* semitone    = "semitone";
    inline constexpr const char* rise        = "rise";
    inline constexpr const char* randRange   = "randRange";
    inline constexpr const char* randSpeed   = "randSpeed";
    inline constexpr const char* quality     = "quality";

    // VOLTAGE / Modulation
    inline constexpr const char* rush        = "rush";
    inline constexpr const char* anger       = "anger";
    inline constexpr const char* modRate     = "modRate";

    // TONE
    inline constexpr const char* lowCut      = "lowCut";
    inline constexpr const char* highCut     = "highCut";
    inline constexpr const char* mid         = "mid";

    // SWARM (chorus)
    inline constexpr const char* swarmOn     = "swarmOn";
    inline constexpr const char* swarmDeep   = "swarmDeep";
    inline constexpr const char* swarmRate   = "swarmRate";
    inline constexpr const char* swarmDepth  = "swarmDepth";
    inline constexpr const char* swarmMix    = "swarmMix";

    // FLOW (rhythmic gate)
    inline constexpr const char* flowOn      = "flowOn";
    inline constexpr const char* flowHard    = "flowHard";
    inline constexpr const char* flowSync    = "flowSync";
    inline constexpr const char* flowAmount  = "flowAmount";
    inline constexpr const char* flowSpeed   = "flowSpeed";
    inline constexpr const char* flowDiv     = "flowDiv";

    // OUTPUT
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* output      = "output";
    inline constexpr const char* bypass      = "bypass";
}

namespace ParamRanges
{
    inline constexpr float lowCutMin   = 20.0f;
    inline constexpr float lowCutMax   = 1000.0f;
    inline constexpr float highCutMin  = 1000.0f;
    inline constexpr float highCutMax  = 20000.0f;
    inline constexpr float midMaxDb    = 12.0f;
    inline constexpr float outputMinDb = -24.0f;
    inline constexpr float outputMaxDb = 12.0f;

    /** Low cut at its minimum and high cut at its maximum mean "filter off". */
    inline bool isLowCutOff  (float hz) { return hz <= lowCutMin + 0.5f; }
    inline bool isHighCutOff (float hz) { return hz >= highCutMax - 1.0f; }
}

namespace ParamChoices
{
    inline const juce::StringArray octaves   { "-2 OCT", "-1 OCT", "0", "+1 OCT", "+2 OCT" };
    inline const juce::StringArray qualities { "Live", "Studio" };
    inline const juce::StringArray divisions { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                               "1/4T", "1/8T", "1/16T", "1/8D", "1/16D" };

    /** Length of each tempo division, in quarter notes. */
    inline double divisionInBeats (int index)
    {
        static constexpr double beats[] { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                          2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 0.75, 0.375 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }

    inline int octaveIndexToSemitones (int index) { return (index - 2) * 12; }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
