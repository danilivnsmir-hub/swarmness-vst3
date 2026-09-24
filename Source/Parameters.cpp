#include "Parameters.h"

namespace
{
    using Attr     = juce::AudioParameterFloatAttributes;
    using Layout   = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Group    = juce::AudioProcessorParameterGroup;

    constexpr int kVersion = 1;

    juce::ParameterID pid (const char* id) { return { id, kVersion }; }

    juce::NormalisableRange<float> skewedRange (float min, float max, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (min, max, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (hz, hz < 10.0f ? 2 : (hz < 100.0f ? 1 : 0)) + " Hz";
    }

    Attr hzAttr()
    {
        return Attr().withLabel ("Hz")
                     .withStringFromValueFunction ([] (float v, int) { return formatHz (v); });
    }

    Attr percentAttr()
    {
        return Attr().withLabel ("%")
                     .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; });
    }

    Attr dbAttr (bool showPlus = true)
    {
        return Attr().withLabel ("dB")
                     .withStringFromValueFunction ([showPlus] (float v, int)
                     {
                         const auto s = juce::String (v, 1);
                         return (showPlus && v > 0.05f ? "+" : "") + s + " dB";
                     });
    }

    Attr msAttr()
    {
        return Attr().withLabel ("ms")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         if (v >= 1000.0f)
                             return juce::String (v / 1000.0f, 2) + " s";
                         return juce::String (juce::roundToInt (v)) + " ms";
                     });
    }

    Attr semitoneAttr (int decimals)
    {
        return Attr().withLabel ("st")
                     .withStringFromValueFunction ([decimals] (float v, int)
                     {
                         return juce::String (v, decimals) + " st";
                     });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace ParamIDs;
    Layout layout;

    // ---------------------------------------------------------------- VOLTAGE
    auto voltage = std::make_unique<Group> ("voltage", "Voltage", "|");

    voltage->addChild (std::make_unique<juce::AudioParameterBool> (pid (pitchOn), "Voltage On", true));

    voltage->addChild (std::make_unique<juce::AudioParameterChoice> (pid (octave), "Octave",
                                                                     ParamChoices::octaves, 3));

    voltage->addChild (std::make_unique<juce::AudioParameterInt> (
        pid (semitone), "Semitone", -12, 12, 0,
        juce::AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction ([] (int v, int)
        {
            return (v > 0 ? "+" : "") + juce::String (v) + " st";
        })));

    voltage->addChild (std::make_unique<juce::AudioParameterFloat> (pid (rise), "Rise",
                                                                    skewedRange (0.0f, 2000.0f, 250.0f, 1.0f), 40.0f, msAttr()));

    voltage->addChild (std::make_unique<juce::AudioParameterFloat> (pid (randRange), "Random Range",
                                                                    skewedRange (0.0f, 24.0f, 6.0f, 0.01f), 0.0f, semitoneAttr (1)));

    voltage->addChild (std::make_unique<juce::AudioParameterFloat> (pid (randSpeed), "Random Speed",
                                                                    skewedRange (0.1f, 20.0f, 2.0f, 0.01f), 2.0f, hzAttr()));

    voltage->addChild (std::make_unique<juce::AudioParameterChoice> (pid (quality), "Quality",
                                                                     ParamChoices::qualities, 1));

    voltage->addChild (std::make_unique<juce::AudioParameterFloat> (pid (rush), "Rush",
                                                                    juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percentAttr()));

    voltage->addChild (std::make_unique<juce::AudioParameterFloat> (pid (anger), "Anger",
                                                                    juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percentAttr()));

    voltage->addChild (std::make_unique<juce::AudioParameterFloat> (pid (modRate), "Mod Rate",
                                                                    skewedRange (0.1f, 20.0f, 2.0f, 0.01f), 1.5f, hzAttr()));

    // ------------------------------------------------------------------- TONE
    auto tone = std::make_unique<Group> ("tone", "Tone", "|");

    tone->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (lowCut), "Low Cut", skewedRange (ParamRanges::lowCutMin, ParamRanges::lowCutMax, 150.0f, 0.1f), 20.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
        {
            return ParamRanges::isLowCutOff (v) ? juce::String ("OFF") : formatHz (v);
        })));

    tone->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (highCut), "High Cut", skewedRange (ParamRanges::highCutMin, ParamRanges::highCutMax, 5000.0f, 1.0f), 20000.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
        {
            return ParamRanges::isHighCutOff (v) ? juce::String ("OFF") : formatHz (v);
        })));

    tone->addChild (std::make_unique<juce::AudioParameterFloat> (pid (mid), "Mid Boost",
                                                                 juce::NormalisableRange<float> (0.0f, ParamRanges::midMaxDb, 0.1f), 0.0f, dbAttr()));

    // ------------------------------------------------------------------ SWARM
    auto swarm = std::make_unique<Group> ("swarm", "Swarm", "|");

    swarm->addChild (std::make_unique<juce::AudioParameterBool> (pid (swarmOn),   "Swarm On",   true));
    swarm->addChild (std::make_unique<juce::AudioParameterBool> (pid (swarmDeep), "Swarm Deep", false));
    swarm->addChild (std::make_unique<juce::AudioParameterFloat> (pid (swarmRate), "Swarm Rate",
                                                                  skewedRange (0.05f, 5.0f, 0.8f, 0.01f), 0.6f, hzAttr()));
    swarm->addChild (std::make_unique<juce::AudioParameterFloat> (pid (swarmDepth), "Swarm Depth",
                                                                  juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percentAttr()));
    swarm->addChild (std::make_unique<juce::AudioParameterFloat> (pid (swarmMix), "Swarm Mix",
                                                                  juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percentAttr()));

    // ------------------------------------------------------------------- FLOW
    auto flow = std::make_unique<Group> ("flow", "Flow", "|");

    flow->addChild (std::make_unique<juce::AudioParameterBool> (pid (flowOn),   "Flow On",   true));
    flow->addChild (std::make_unique<juce::AudioParameterBool> (pid (flowHard), "Flow Hard", true));
    flow->addChild (std::make_unique<juce::AudioParameterBool> (pid (flowSync), "Flow Sync", false));
    flow->addChild (std::make_unique<juce::AudioParameterFloat> (pid (flowAmount), "Flow Amount",
                                                                 juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percentAttr()));
    flow->addChild (std::make_unique<juce::AudioParameterFloat> (pid (flowSpeed), "Flow Speed",
                                                                 skewedRange (0.5f, 20.0f, 4.0f, 0.01f), 4.0f, hzAttr()));
    flow->addChild (std::make_unique<juce::AudioParameterChoice> (pid (flowDiv), "Flow Division",
                                                                  ParamChoices::divisions, 3));

    // ----------------------------------------------------------------- OUTPUT
    auto out = std::make_unique<Group> ("output", "Output", "|");

    out->addChild (std::make_unique<juce::AudioParameterFloat> (pid (mix), "Mix",
                                                                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percentAttr()));
    out->addChild (std::make_unique<juce::AudioParameterFloat> (pid (drive), "Drive",
                                                                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percentAttr()));
    out->addChild (std::make_unique<juce::AudioParameterFloat> (pid (output), "Output",
                                                                juce::NormalisableRange<float> (ParamRanges::outputMinDb, ParamRanges::outputMaxDb, 0.1f),
                                                                0.0f, dbAttr()));
    out->addChild (std::make_unique<juce::AudioParameterBool> (pid (bypass), "Bypass", false));

    layout.add (std::move (voltage), std::move (tone), std::move (swarm), std::move (flow), std::move (out));
    return layout;
}
