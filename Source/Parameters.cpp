#include "Parameters.h"

namespace
{
    using Attr   = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Group  = juce::AudioProcessorParameterGroup;

    constexpr int kVersion = 3;

    juce::ParameterID pid (const char* id) { return { id, kVersion }; }

    juce::NormalisableRange<float> skewedRange (float min, float max, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (min, max, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::NormalisableRange<float> percentRange() { return { 0.0f, 100.0f, 0.1f }; }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";
        return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz";
    }

    Attr hzAttr()
    {
        return Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return formatHz (v); });
    }

    Attr percentAttr()
    {
        return Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; });
    }

    std::unique_ptr<juce::AudioParameterBool> toggle (const char* id, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (pid (id), name, def);
    }

    std::unique_ptr<juce::AudioParameterFloat> percent (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (pid (id), name, percentRange(), def, percentAttr());
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace ParamIDs;
    Layout layout;

    // ------------------------------------------------------------------ NOISE
    auto noise = std::make_unique<Group> ("noise", "Noise", "|");
    noise->addChild (toggle (oct1, "+1 Octave", false));
    noise->addChild (toggle (oct2, "+2 Octaves", false));
    noise->addChild (toggle (noiseDown, "Noise Down", false));
    noise->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (rise), "Rise", skewedRange (0.0f, 2000.0f, 250.0f, 1.0f), 30.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
        })));
    noise->addChild (percent (panic, "Panic", 0.0f));
    noise->addChild (percent (chaos, "Chaos", 0.0f));
    noise->addChild (percent (speed, "Speed", 0.0f));

    // ---------------------------------------------------------------- RAINBOW
    auto rainbow = std::make_unique<Group> ("rainbow", "Rainbow", "|");
    rainbow->addChild (toggle (rbOn, "Rainbow On", false));
    rainbow->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (rbPitch), "Rainbow Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 7.0f,
        Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.005f ? "+" : "") + juce::String (v, 2) + " st";
        })));
    rainbow->addChild (toggle (rbSnap, "Rainbow Snap", true));
    rainbow->addChild (percent (rbPrimary, "Primary", 60.0f));
    rainbow->addChild (percent (rbSecondary, "Secondary", 0.0f));
    rainbow->addChild (percent (rbTone, "Rainbow Tone", 60.0f));
    rainbow->addChild (percent (rbTracking, "Tracking", 80.0f));
    rainbow->addChild (percent (rbMagic, "Magic", 0.0f));
    rainbow->addChild (toggle (magicHold, "Magic Switch", false));

    // ------------------------------------------------------------------ SWARM
    auto swarm = std::make_unique<Group> ("swarm", "Swarm", "|");
    swarm->addChild (toggle (swarmOn, "Swarm On", false));
    swarm->addChild (toggle (swarmDeep, "Swarm Deep", false));
    swarm->addChild (std::make_unique<juce::AudioParameterFloat> (pid (swarmRate), "Swarm Rate",
                                                                  skewedRange (0.05f, 8.0f, 0.8f, 0.01f), 0.6f, hzAttr()));
    swarm->addChild (percent (swarmDepth, "Swarm Depth", 50.0f));
    swarm->addChild (percent (swarmMix, "Swarm Mix", 50.0f));

    // ------------------------------------------------------------------- FUZZ
    auto fz = std::make_unique<Group> ("fuzz", "Fuzz", "|");
    fz->addChild (toggle (fuzzOn, "Fuzz On", false));
    fz->addChild (toggle (fuzzPost, "Fuzz Post", false));
    fz->addChild (percent (fuzz, "Fuzz", 60.0f));
    fz->addChild (percent (fuzzTone, "Fuzz Tone", 50.0f));
    fz->addChild (percent (fuzzGate, "Fuzz Gate", 0.0f));

    // ------------------------------------------------------------------- FLOW
    auto flow = std::make_unique<Group> ("flow", "Flow", "|");
    flow->addChild (toggle (flowOn, "Flow On", false));
    flow->addChild (toggle (flowHard, "Flow Hard", true));
    flow->addChild (toggle (flowSync, "Flow Sync", false));
    flow->addChild (percent (flowAmount, "Flow Amount", 100.0f));
    flow->addChild (std::make_unique<juce::AudioParameterFloat> (pid (flowSpeed), "Flow Speed",
                                                                 skewedRange (0.5f, 30.0f, 5.0f, 0.01f), 8.0f, hzAttr()));
    flow->addChild (std::make_unique<juce::AudioParameterChoice> (pid (flowDiv), "Flow Division", ParamChoices::divisions, 4));

    // ----------------------------------------------------------------- OUTPUT
    auto out = std::make_unique<Group> ("output", "Output", "|");
    out->addChild (percent (mix, "Mix", 100.0f));
    out->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (output), "Output", juce::NormalisableRange<float> (ParamRanges::outputMinDb, ParamRanges::outputMaxDb, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        })));
    out->addChild (std::make_unique<juce::AudioParameterChoice> (pid (switchMode), "Footswitch Mode", ParamChoices::switchModes, 0));
    out->addChild (toggle (bypass, "Bypass", false));

    layout.add (std::move (noise), std::move (rainbow), std::move (swarm), std::move (fz), std::move (flow), std::move (out));
    return layout;
}
