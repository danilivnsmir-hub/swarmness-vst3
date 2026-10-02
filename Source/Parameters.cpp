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

    std::unique_ptr<juce::AudioParameterFloat> cents (const char* id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, juce::NormalisableRange<float> (-50.0f, 50.0f, 1.0f), 0.0f,
            Attr().withLabel ("ct").withStringFromValueFunction ([] (float v, int)
            {
                const int c = juce::roundToInt (v);
                return (c > 0 ? "+" : "") + juce::String (c) + " ct";
            }));
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";
        if (hz >= 100.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz";
    }

    /** "+3.0 dB", "0.0 dB" (never "-0.0"), "-6.5 dB". */
    juce::String formatDb (float v)
    {
        if (std::abs (v) < 0.05f)
            v = 0.0f;
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
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

    // ------------------------------------------------------------------ SHIFT and HIVE
    // (two groups, SHIFT first: the flat parameter order is the one hosts have always seen)
    auto shift = std::make_unique<Group> ("shift", "Shift", "|");
    auto hive = std::make_unique<Group> ("hive", "Hive", "|");
    auto intervalParam = [] (const char* id, const juce::String& name, int def)
    {
        return std::make_unique<juce::AudioParameterInt> (
            pid (id), name, -24, 24, def,
            juce::AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction ([] (int v, int) { return ParamChoices::intervalName (v); }));
    };
    auto glideTime = [] (const char* id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, skewedRange (0.0f, 2000.0f, 250.0f, 1.0f), 30.0f,
            Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
            }));
    };

    // SHIFT
    shift->addChild (toggle (oct1, "Shift A (footswitch)", false));
    shift->addChild (toggle (oct2, "Shift B (footswitch)", false));
    shift->addChild (intervalParam (shiftA, "Shift A Interval", 12));
    shift->addChild (intervalParam (shiftB, "Shift B Interval", 24));
    shift->addChild (glideTime (rise, "Rise"));
    shift->addChild (glideTime (fall, "Fall"));
    shift->addChild (toggle (shOn, "Shift On", false));
    shift->addChild (percent (stingMix, "Shift Mix", 100.0f));
    shift->addChild (toggle (shStack, "Shift Stack A+B", false));
    shift->addChild (percent (panic, "Shift Anger", 0.0f));
    shift->addChild (percent (chaos, "Shift Frenzy", 0.0f));
    shift->addChild (percent (speed, "Shift Buzz", 0.0f));
    shift->addChild (toggle (shSnap, "Shift Snap", true));
    shift->addChild (toggle (shRaw, "Shift Raw", true));
    shift->addChild (cents (shDetune, "Shift Detune"));

    // VOICES
    hive->addChild (toggle (rbOn, "Hive On", false));
    hive->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (rbPitch), "Hive Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 7.0f,
        Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int)
        {
            // Whole semitones also show the interval name
            const float r = std::round (v);
            if (std::abs (v - r) < 0.005f)
                return ParamChoices::intervalName ((int) r);
            return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
        })));
    hive->addChild (toggle (rbSnap, "Hive Snap", true));
    hive->addChild (percent (rbPrimary, "Hive Drone", 60.0f));
    hive->addChild (percent (rbSecondary, "Hive Queen", 0.0f));
    hive->addChild (percent (rbTracking, "Hive Tracking", 80.0f));

    // TRAILS
    hive->addChild (percent (rbMagic, "Hive Trails", 0.0f));
    hive->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (rbTime), "Hive Time", skewedRange (40.0f, 1200.0f, 250.0f, 1.0f), 180.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
        })));
    hive->addChild (toggle (rbSync, "Hive Sync", false));
    hive->addChild (std::make_unique<juce::AudioParameterChoice> (pid (rbDiv), "Hive Division", ParamChoices::divisions, 3));
    hive->addChild (percent (rbTone, "Hive Tone", 60.0f));
    hive->addChild (std::make_unique<juce::AudioParameterInt> (pid (trSteps), "Trails Steps", 1, 16, 8));
    hive->addChild (percent (trChop, "Trails Gate", 0.0f));
    hive->addChild (toggle (trDry, "Trails From Dry", false));
    for (int k = 0; k < 16; ++k)
    {
        hive->addChild (std::make_unique<juce::AudioParameterFloat> (
            pid (trLevels[k]), "Trails Step " + juce::String (k + 1) + " Level", percentRange(), 100.0f,
            Attr().withLabel ("%").withAutomatable (false)));
        hive->addChild (std::make_unique<juce::AudioParameterChoice> (
            pid (trMoves[k]), "Trails Step " + juce::String (k + 1) + " Move", ParamChoices::stepMoves, 1,
            juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    }
    hive->addChild (toggle (magicHold, "Venom (footswitch)", false));
    hive->addChild (toggle (linkOct1, "Venom Links Shift A", false));
    hive->addChild (toggle (linkOct2, "Venom Links Shift B", false));

    // MANGLE
    hive->addChild (percent (hvMangle, "Hive Mangle", 0.0f));
    hive->addChild (toggle (rbRaw, "Hive Raw", true));
    hive->addChild (cents (rbDetune, "Hive Detune"));
    hive->addChild (percent (rbMix, "Hive Mix", 50.0f));

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
    fz->addChild (percent (fuzz, "Smoke Fuzz", 70.0f));
    fz->addChild (percent (fuzzTone, "Smoke Tone", 50.0f));
    fz->addChild (std::make_unique<juce::AudioParameterChoice> (pid (fuzzVoice), "Smoke Voice", ParamChoices::fuzzVoices, 1));
    fz->addChild (percent (fuzzScoop, "Smoke Scoop", 40.0f));
    fz->addChild (percent (fuzzGlare, "Smoke Glare", 0.0f));
    fz->addChild (percent (fuzzGate, "Smoke Gate", 0.0f));
    fz->addChild (percent (fuzzBlend, "Smoke Clean", 0.0f));
    fz->addChild (percent (fuzzSag, "Smoke Sag", 40.0f));

    // ------------------------------------------------------------------- FLOW
    auto flow = std::make_unique<Group> ("flow", "Wings", "|");
    flow->addChild (toggle (flowOn, "Wings On", false));
    flow->addChild (toggle (flowHard, "Wings Hard", true));
    flow->addChild (toggle (flowSync, "Wings Sync", false));
    flow->addChild (percent (flowAmount, "Wings Amount", 100.0f));
    flow->addChild (std::make_unique<juce::AudioParameterFloat> (pid (flowSpeed), "Wings Speed",
                                                                 skewedRange (0.5f, 30.0f, 5.0f, 0.01f), 8.0f, hzAttr()));
    flow->addChild (std::make_unique<juce::AudioParameterChoice> (pid (flowDiv), "Wings Division", ParamChoices::divisions, 4));

    auto dbAttr = []
    {
        return Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return formatDb (v);
        });
    };
    auto gainParam = [&] (const char* id, const juce::String& name, float maxDb)
    {
        return std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (-maxDb, maxDb, 0.1f), 0.0f, dbAttr());
    };
    auto freqParam = [] (const char* id, const juce::String& name, float min, float max, float centre, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (pid (id), name, skewedRange (min, max, centre, 0.1f), def, hzAttr());
    };
    auto qParam = [] (const char* id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (pid (id), name, skewedRange (0.3f, 10.0f, 1.2f, 0.01f), 1.0f,
                                                            Attr().withStringFromValueFunction ([] (float v, int) { return "Q " + juce::String (v, 2); }));
    };

    // ------------------------------------------------------------ COMB (graphic EQ)
    auto comb = std::make_unique<Group> ("comb", "Comb EQ", "|");
    comb->addChild (toggle (geqOn, "Comb On", false));
    for (int i = 0; i < 10; ++i)
    {
        const float hz = ParamRanges::geqBandHz[i];
        const auto label = hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
        comb->addChild (gainParam (geqBands[i], "Comb " + label, ParamRanges::geqMaxDb));
    }
    comb->addChild (gainParam (geqLevel, "Comb Level", ParamRanges::geqMaxDb));

    // ---------------------------------------------------------- CARVE (parametric EQ)
    auto carve = std::make_unique<Group> ("carve", "Carve EQ", "|");
    carve->addChild (toggle (peqOn, "Carve On", false));
    carve->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (peqHpFreq), "Carve Low Cut", skewedRange (ParamRanges::peqHpOff, 1000.0f, 90.0f, 0.1f), ParamRanges::peqHpOff,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v <= ParamRanges::peqHpOff + 0.05f ? juce::String ("Off") : formatHz (v); })));
    carve->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (peqLpFreq), "Carve High Cut", skewedRange (1000.0f, ParamRanges::peqLpOff, 6000.0f, 1.0f), ParamRanges::peqLpOff,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v >= ParamRanges::peqLpOff - 0.5f ? juce::String ("Off") : formatHz (v); })));
    carve->addChild (freqParam (peqLowFreq, "Carve Low Shelf Freq", 30.0f, 600.0f, 120.0f, 100.0f));
    carve->addChild (gainParam (peqLowGain, "Carve Low Shelf Gain", ParamRanges::peqMaxDb));
    carve->addChild (freqParam (peqB1Freq, "Carve Bell 1 Freq", 30.0f, 16000.0f, 700.0f, 250.0f));
    carve->addChild (gainParam (peqB1Gain, "Carve Bell 1 Gain", ParamRanges::peqMaxDb));
    carve->addChild (qParam (peqB1Q, "Carve Bell 1 Q"));
    carve->addChild (freqParam (peqB2Freq, "Carve Bell 2 Freq", 30.0f, 16000.0f, 700.0f, 800.0f));
    carve->addChild (gainParam (peqB2Gain, "Carve Bell 2 Gain", ParamRanges::peqMaxDb));
    carve->addChild (qParam (peqB2Q, "Carve Bell 2 Q"));
    carve->addChild (freqParam (peqB3Freq, "Carve Bell 3 Freq", 30.0f, 16000.0f, 700.0f, 3000.0f));
    carve->addChild (gainParam (peqB3Gain, "Carve Bell 3 Gain", ParamRanges::peqMaxDb));
    carve->addChild (qParam (peqB3Q, "Carve Bell 3 Q"));
    carve->addChild (freqParam (peqHighFreq, "Carve High Shelf Freq", 1500.0f, 16000.0f, 5000.0f, 6000.0f));
    carve->addChild (gainParam (peqHighGain, "Carve High Shelf Gain", ParamRanges::peqMaxDb));

    // ----------------------------------------------------------------- CRYPT (reverb)
    auto crypt = std::make_unique<Group> ("crypt", "Crypt Reverb", "|");
    crypt->addChild (toggle (revOn, "Crypt On", false));
    crypt->addChild (std::make_unique<juce::AudioParameterChoice> (pid (revType), "Crypt Type", ParamChoices::reverbTypes, 2));
    crypt->addChild (percent (revMix, "Crypt Mix", 25.0f));
    crypt->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (revDecay), "Crypt Decay", skewedRange (0.2f, 20.0f, 2.5f, 0.01f), 2.5f,
        Attr().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; })));
    crypt->addChild (percent (revSize, "Crypt Size", 60.0f));
    crypt->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (revPreDelay), "Crypt Pre-Delay", skewedRange (0.0f, 250.0f, 40.0f, 0.1f), 15.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    crypt->addChild (percent (revTone, "Crypt Tone", 50.0f));
    crypt->addChild (freqParam (revLowCut, "Crypt Low Cut", 20.0f, 800.0f, 150.0f, 150.0f));
    crypt->addChild (percent (revMod, "Crypt Mod", 30.0f));
    crypt->addChild (percent (revDuck, "Crypt Duck", 0.0f));

    // ------------------------------------------------------------------ AMP
    auto knob10 = [] (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
            Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
    };
    auto levelParam = [&] (const char* id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, dbAttr());
    };
    auto amp = std::make_unique<Group> ("amp", "Amp", "|");
    amp->addChild (toggle (ampOn, "Amp On", false));
    amp->addChild (std::make_unique<juce::AudioParameterChoice> (pid (ampChannel), "Amp Channel", ParamChoices::ampChannels, 1));
    amp->addChild (knob10 (ampGain, "Amp Gain", 5.0f));
    amp->addChild (knob10 (ampBass, "Amp Bass", 5.0f));
    amp->addChild (knob10 (ampMid, "Amp Mid", 5.0f));
    amp->addChild (knob10 (ampTreble, "Amp Treble", 5.0f));
    amp->addChild (knob10 (ampPresence, "Amp Presence", 5.0f));
    amp->addChild (knob10 (ampDepth, "Amp Depth", 5.0f));
    amp->addChild (knob10 (ampMaster, "Amp Master", 5.0f));
    amp->addChild (percent (ampGate, "Amp Gate", 0.0f));
    amp->addChild (levelParam (ampLevel, "Amp Level"));
    amp->addChild (knob10 (namInput, "Amp NAM Input", 5.0f));
    amp->addChild (knob10 (namBass, "NAM Bass", 5.0f));
    amp->addChild (knob10 (namMid, "NAM Mid", 5.0f));
    amp->addChild (knob10 (namTreble, "NAM Treble", 5.0f));
    amp->addChild (knob10 (namPresence, "NAM Presence", 5.0f));
    amp->addChild (knob10 (namDepth, "NAM Depth", 5.0f));
    amp->addChild (knob10 (namOutput, "Amp NAM Output", 5.0f));
    amp->addChild (toggle (namLite, "NAM A2 Lite", false));

    // ------------------------------------------------------------------ WASP
    auto drv = std::make_unique<Group> ("wasp", "Wasp", "|");
    drv->addChild (toggle (drvOn, "Wasp On", false));
    drv->addChild (knob10 (drvVolume, "Wasp Volume", 5.0f));
    drv->addChild (knob10 (drvDrive, "Wasp Drive", 3.0f));
    drv->addChild (knob10 (drvBright, "Wasp Bright", 5.0f));
    drv->addChild (knob10 (drvAttack, "Wasp Attack", 5.0f));
    drv->addChild (percent (drvGate, "Wasp Gate", 0.0f));
    drv->addChild (toggle (drvNam, "Wasp NAM Mode", false));
    drv->addChild (knob10 (drvNamInput, "Wasp NAM Input", 5.0f));
    drv->addChild (knob10 (drvNamOutput, "Wasp NAM Output", 5.0f));
    drv->addChild (toggle (drvNamLite, "Wasp NAM A2 Lite", false));

    // ------------------------------------------------------------------ CAB
    auto cab = std::make_unique<Group> ("cab", "Cab", "|");
    cab->addChild (toggle (cabOn, "Cab On", false));
    cab->addChild (std::make_unique<juce::AudioParameterChoice> (pid (cabType), "Cab Type", ParamChoices::cabTypes, 3));
    cab->addChild (percent (cabMic, "Cab Mic Position", 30.0f));
    cab->addChild (percent (cabDist, "Cab Mic Distance", 20.0f));
    cab->addChild (freqParam (cabLowCut, "Cab Low Cut", 20.0f, 400.0f, 80.0f, 60.0f));
    cab->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (cabHighCut), "Cab High Cut", skewedRange (2000.0f, 20000.0f, 7000.0f, 1.0f), 20000.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v >= 19900.0f ? juce::String ("Off") : formatHz (v); })));
    cab->addChild (levelParam (cabLevel, "Cab Level"));
    cab->addChild (percent (cabIrMix, "Cab IR Mix A/B", 50.0f));
    cab->addChild (toggle (cabIrInvB, "Cab IR B Phase Invert", false));

    // ------------------------------------------------------------------ CHAIN ORDER
    auto chain = std::make_unique<Group> ("chain", "Chain", "|");
    auto slotParam = [] (int b) { return std::make_unique<juce::AudioParameterInt> (
            pid (Chain::slotIds[b]), juce::String ("Chain Slot ") + Chain::names[b], 0, Chain::slotMax, Chain::defaultSlots[b],
            juce::AudioParameterIntAttributes().withAutomatable (false)); };
    auto laneParam = [] (int b) { return std::make_unique<juce::AudioParameterChoice> (
            pid (Chain::laneIds[b]), juce::String ("Chain Lane ") + Chain::names[b],
            juce::StringArray { "Series", "Parallel A", "Parallel B" }, Chain::series,
            juce::AudioParameterChoiceAttributes().withAutomatable (false)); };
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (b != Chain::honey)   // HONEY's slot / lane sit in its own group (added in 1.1, after everything else)
            chain->addChild (slotParam (b));
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (b != Chain::honey)
            chain->addChild (laneParam (b));
    for (int sp = 0; sp < Chain::maxSplits; ++sp)
        chain->addChild (std::make_unique<juce::AudioParameterFloat> (
            pid (Chain::parallelMixIds[sp]), "Split " + juce::String (sp + 1) + " Mix", percentRange(), 50.0f,
            Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
            {
                const int b = juce::roundToInt (v);
                return "A " + juce::String (100 - b) + " / B " + juce::String (b);
            })));

    // ----------------------------------------------------------------- OUTPUT
    auto out = std::make_unique<Group> ("output", "Output", "|");
    out->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (input), "Input", juce::NormalisableRange<float> (ParamRanges::inputMinDb, ParamRanges::inputMaxDb, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return formatDb (v);
        })));
    out->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (output), "Volume", juce::NormalisableRange<float> (ParamRanges::outputMinDb, ParamRanges::outputMaxDb, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return formatDb (v);
        })));
    out->addChild (std::make_unique<juce::AudioParameterChoice> (pid (switchMode), "Footswitch Mode", ParamChoices::switchModes, 0));
    // meta: switching the scene changes other parameters
    out->addChild (std::make_unique<juce::AudioParameterChoice> (pid (scene), "Scene", ParamChoices::scenes, 0,
                                                                 juce::AudioParameterChoiceAttributes().withMeta (true)));
    out->addChild (toggle (bypass, "Bypass", false));

    // ------------------------------------------------------------------ HONEY (1.1: last, so the parameter order hosts saw before is unchanged)
    auto honey = std::make_unique<Group> ("honey", "Honey", "|");
    honey->addChild (toggle (hnOn, "Honey On", false));
    honey->addChild (percent (hnSustain, "Honey Sustain", 50.0f));
    honey->addChild (percent (hnAttack, "Honey Attack", 50.0f));
    honey->addChild (percent (hnBlend, "Honey Blend", 100.0f));
    honey->addChild (std::make_unique<juce::AudioParameterFloat> (pid (hnLevel), "Honey Level", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));
    honey->addChild (toggle (hnLimit, "Honey Limit", false));
    honey->addChild (slotParam (Chain::honey));
    honey->addChild (laneParam (Chain::honey));

    // ------------------------------------------------------------------ WASP character (1.1, appended for the same reason)
    auto drvCh = std::make_unique<Group> ("waspCharacter", "Wasp Character", "|");
    drvCh->addChild (std::make_unique<juce::AudioParameterChoice> (pid (drvCharacter), "Wasp Character", ParamChoices::waspCharacters, 0));

    // ------------------------------------------------------------------ WINGS steps (1.1, appended)
    auto wings = std::make_unique<Group> ("wingsSteps", "Wings Steps", "|");
    wings->addChild (std::make_unique<juce::AudioParameterInt> (pid (wgSteps), "Wings Steps", 1, 16, 2));
    for (int k = 0; k < 16; ++k)
        wings->addChild (std::make_unique<juce::AudioParameterFloat> (
            pid (wgLevels[k]), "Wings Step " + juce::String (k + 1) + " Level", percentRange(), k % 2 == 0 ? 100.0f : 0.0f,
            Attr().withLabel ("%").withAutomatable (false)));

    // ------------------------------------------------------------------ CRYPT freeze (1.1, appended)
    auto cryptFz = std::make_unique<Group> ("cryptFreeze", "Crypt Freeze", "|");
    cryptFz->addChild (toggle (revFreeze, "Crypt Freeze", false));

    // ------------------------------------------------------------------ STOMPS (1.1, appended): STING and what VENOM / STING engage
    auto stomps = std::make_unique<Group> ("stomps", "Stomps", "|");
    stomps->addChild (toggle (stingHold, "Sting (footswitch)", false));
    stomps->addChild (toggle (stingShiftA, "Sting Links Shift A", false));
    stomps->addChild (toggle (stingShiftB, "Sting Links Shift B", false));
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (venomBlock[b] != nullptr)
            stomps->addChild (std::make_unique<juce::AudioParameterChoice> (pid (venomBlock[b]), "Venom " + juce::String (Chain::names[b]),
                                                                            ParamChoices::stompActions, b == Chain::pitch ? 1 : 0));
    stomps->addChild (std::make_unique<juce::AudioParameterChoice> (pid (venomFreeze), "Venom Freeze", ParamChoices::stompActions, 0));
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (stingBlock[b] != nullptr)
            stomps->addChild (std::make_unique<juce::AudioParameterChoice> (pid (stingBlock[b]), "Sting " + juce::String (Chain::names[b]),
                                                                            ParamChoices::stompActions, 0));
    stomps->addChild (std::make_unique<juce::AudioParameterChoice> (pid (stingFreeze), "Sting Freeze", ParamChoices::stompActions, 0));

    // ------------------------------------------------------------------ HIVE STOP (1.1, appended)
    auto hvSt = std::make_unique<Group> ("hiveStop", "Hive Stop", "|");
    hvSt->addChild (toggle (hvStop, "Hive Stop", false));
    hvSt->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (hvStopTime), "Hive Stop Time", skewedRange (0.1f, 3.0f, 0.8f, 0.01f), 0.8f,
        Attr().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " s"; })));
    hvSt->addChild (std::make_unique<juce::AudioParameterChoice> (pid (venomStop), "Venom Hive Stop", ParamChoices::stompActions, 0));
    hvSt->addChild (std::make_unique<juce::AudioParameterChoice> (pid (stingStop), "Sting Hive Stop", ParamChoices::stompActions, 0));

    // ------------------------------------------------------------------ SMOKE CRUSH + SWARM RING (1.1, appended)
    auto tricks = std::make_unique<Group> ("tricks", "Smoke Crush / Swarm Ring", "|");
    tricks->addChild (percent (fuzzCrush, "Smoke Crush", 0.0f));
    tricks->addChild (percent (swarmRing, "Swarm Ring", 0.0f));

    layout.add (std::move (shift), std::move (hive), std::move (swarm), std::move (fz), std::move (flow),
                std::move (comb), std::move (carve), std::move (crypt), std::move (amp), std::move (cab), std::move (drv), std::move (chain), std::move (out),
                std::move (honey), std::move (drvCh), std::move (wings), std::move (cryptFz), std::move (stomps), std::move (hvSt),
                std::move (tricks));
    return layout;
}
