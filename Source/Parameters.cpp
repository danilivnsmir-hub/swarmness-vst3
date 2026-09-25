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
    auto noise = std::make_unique<Group> ("noise", "Sting", "|");
    noise->addChild (toggle (oct1, "+1 Octave", false));
    noise->addChild (toggle (oct2, "+2 Octaves", false));
    noise->addChild (toggle (noiseDown, "Sting Dive", false));
    auto glideTime = [] (const char* id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, skewedRange (0.0f, 2000.0f, 250.0f, 1.0f), 30.0f,
            Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
            }));
    };
    noise->addChild (glideTime (rise, "Rise"));
    noise->addChild (glideTime (fall, "Fall"));
    noise->addChild (percent (panic, "Anger", 0.0f));
    noise->addChild (percent (chaos, "Frenzy", 0.0f));
    noise->addChild (percent (speed, "Buzz", 0.0f));
    noise->addChild (percent (stingMix, "Sting Mix", 100.0f));

    // ---------------------------------------------------------------- RAINBOW
    auto rainbow = std::make_unique<Group> ("rainbow", "Hive", "|");
    rainbow->addChild (toggle (rbOn, "Hive On", false));
    rainbow->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (rbPitch), "Hive Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 7.0f,
        Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.005f ? "+" : "") + juce::String (v, 2) + " st";
        })));
    rainbow->addChild (toggle (rbSnap, "Hive Snap", true));
    rainbow->addChild (percent (rbPrimary, "Drone", 60.0f));
    rainbow->addChild (percent (rbSecondary, "Queen", 0.0f));
    rainbow->addChild (percent (rbTone, "Hive Tone", 60.0f));
    rainbow->addChild (percent (rbTracking, "Hive Tracking", 80.0f));
    rainbow->addChild (percent (rbMagic, "Hive Trails", 0.0f));
    rainbow->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (rbTime), "Hive Time", skewedRange (40.0f, 1200.0f, 250.0f, 1.0f), 180.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
        })));
    rainbow->addChild (toggle (rbSync, "Hive Sync", false));
    rainbow->addChild (std::make_unique<juce::AudioParameterChoice> (pid (rbDiv), "Hive Division", ParamChoices::divisions, 3));
    rainbow->addChild (toggle (magicHold, "Venom Switch", false));
    rainbow->addChild (toggle (linkOct1, "Venom Links +1 Oct", false));
    rainbow->addChild (toggle (linkOct2, "Venom Links +2 Oct", false));

    // ------------------------------------------------------------------ SWARM
    auto swarm = std::make_unique<Group> ("swarm", "Swarm", "|");
    swarm->addChild (toggle (swarmOn, "Swarm On", false));
    swarm->addChild (toggle (swarmDeep, "Swarm Deep", false));
    swarm->addChild (std::make_unique<juce::AudioParameterFloat> (pid (swarmRate), "Swarm Rate",
                                                                  skewedRange (0.05f, 8.0f, 0.8f, 0.01f), 0.6f, hzAttr()));
    swarm->addChild (percent (swarmDepth, "Swarm Depth", 50.0f));
    swarm->addChild (percent (swarmMix, "Swarm Mix", 50.0f));

    // ------------------------------------------------------------------- FUZZ
    auto fz = std::make_unique<Group> ("fuzz", "Smoke", "|");
    fz->addChild (toggle (fuzzOn, "Smoke On", false));
    fz->addChild (toggle (fuzzPost, "Smoke Post", false));
    fz->addChild (percent (fuzz, "Smoke Fuzz", 70.0f));
    fz->addChild (percent (fuzzTone, "Smoke Tone", 50.0f));
    fz->addChild (std::make_unique<juce::AudioParameterChoice> (pid (fuzzVoice), "Smoke Voice", ParamChoices::fuzzVoices, 1));
    fz->addChild (percent (fuzzScoop, "Smoke Scoop", 40.0f));
    fz->addChild (percent (fuzzGlare, "Smoke Glare", 0.0f));
    fz->addChild (percent (fuzzGate, "Smoke Gate", 0.0f));
    fz->addChild (percent (fuzzBlend, "Smoke Blend", 0.0f));

    // ------------------------------------------------------------------- FLOW
    auto flow = std::make_unique<Group> ("flow", "Wings", "|");
    flow->addChild (toggle (flowOn, "Wings On", false));
    flow->addChild (toggle (flowHard, "Wings Hard", true));
    flow->addChild (toggle (flowSync, "Wings Sync", false));
    flow->addChild (percent (flowAmount, "Wings Amount", 100.0f));
    flow->addChild (std::make_unique<juce::AudioParameterFloat> (pid (flowSpeed), "Wings Speed",
                                                                 skewedRange (0.5f, 30.0f, 5.0f, 0.01f), 8.0f, hzAttr()));
    flow->addChild (std::make_unique<juce::AudioParameterChoice> (pid (flowDiv), "Wings Division", ParamChoices::divisions, 4));

    // ----------------------------------------------------------------- OUTPUT
    auto out = std::make_unique<Group> ("output", "Output", "|");
    out->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (input), "Input", juce::NormalisableRange<float> (ParamRanges::inputMinDb, ParamRanges::inputMaxDb, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        })));
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
