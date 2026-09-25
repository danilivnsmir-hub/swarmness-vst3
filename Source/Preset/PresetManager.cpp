#include "PresetManager.h"
#include "../Parameters.h"

const juce::String PresetManager::extension = ".swpreset";

namespace
{
    /** Footswitches and the footswitch mode are performance settings, not part of a sound. */
    bool isPresetParameter (const juce::String& id)
    {
        return ! isPerformanceParameter (id) && id != ParamIDs::switchMode;
    }

    juce::String sanitiseName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).substring (0, 64);
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    initialiseFactoryPresets();
    takeSnapshot();
}

//==============================================================================
void PresetManager::initialiseFactoryPresets()
{
    using namespace ParamIDs;

    // Presets store the sound; the footswitches (+1 / +2 OCT, VENOM) are played live.
    // Internal IDs: noise* = STING, rb* = HIVE, fuzz* = SMOKE, flow* = WINGS, panic = ANGER,
    // chaos = FRENZY, speed = BUZZ, noiseDown = DIVE, rbPrimary = DRONE, rbSecondary = QUEEN, rbMagic = VENOM.
    // Wings division indices: 3 = 1/8, 4 = 1/16, 5 = 1/32
    const juce::String basics ("Basics"), stingCat ("Sting - hold +1/+2 OCT"), hiveCat ("Hive"),
                       texture ("Smoke, Swarm & Wings"), attack ("Swarm Attack");

    factoryPresets = {
        // ---------------------------------------------------------------- basics
        { "Init", basics, "Everything off: the plug-in is transparent. Start here.", {} },
        { "Clean Sting", basics, "Pure octave shifter. Hold +1 OCT / +2 OCT for a clean, instant jump.",
          { { rise, 0 } } },
        { "Slow Rise", basics, "Hold a footswitch and the pitch sweeps up over ~1 s; release and it sweeps back down.",
          { { rise, 950 } } },

        // ----------------------------------------------------------------- STING
        { "Killer Bee", stingCat, "The all-rounder: a bit of ANGER, FRENZY and BUZZ plus SMOKE in front. Hold +2 OCT for the full shriek.",
          { { rise, 30 }, { panic, 45 }, { chaos, 30 }, { speed, 25 }, { fuzzOn, 1 }, { fuzz, 55 }, { fuzzTone, 55 } } },
        { "Angry Hive", stingCat, "ANGER only: the octave is torn into two detuned voices - sour, beating dissonance.",
          { { rise, 15 }, { panic, 85 } } },
        { "Frenzy", stingCat, "FRENZY only: the pitch jumps randomly around the octave, faster than your picking.",
          { { rise, 10 }, { chaos, 75 } } },
        { "Lazy Buzz", stingCat, "Low BUZZ: slow phasing and wobble on a slightly angry octave.",
          { { rise, 120 }, { speed, 22 }, { panic, 25 } } },
        { "Hornet Buzz", stingCat, "High BUZZ: all-pass feedback and AM turn the octave into metallic ring-mod noise.",
          { { rise, 0 }, { speed, 92 } } },
        { "Dive Bomb", stingCat, "DIVE with a long RISE: hold for a sub-octave dive, release to climb back. SMOKE after.",
          { { noiseDown, 1 }, { rise, 450 }, { panic, 15 }, { fuzzOn, 1 }, { fuzzPost, 1 }, { fuzz, 55 }, { fuzzTone, 35 } } },

        // ------------------------------------------------------------------ HIVE
        { "Harmony Fifth", hiveCat, "Tight, clean harmony: a fifth above (DRONE) plus its octave (QUEEN). No VENOM.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbSecondary, 25 }, { rbTracking, 95 }, { rbTone, 70 } } },
        { "Atonal Detune", hiveCat, "SNAP off: a quarter-tone-flat double. Instantly wrong in the best way.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -0.4f }, { rbPrimary, 85 }, { rbTracking, 100 }, { rbTone, 65 } } },
        { "Tone Clusters", hiveCat, "Low TRACKING: the harmony lags and repeats grains, smearing into rhythmic clusters.",
          { { rbOn, 1 }, { rbPitch, 5 }, { rbPrimary, 70 }, { rbSecondary, 20 }, { rbTracking, 8 }, { rbMagic, 20 }, { rbTone, 55 } } },
        { "Honey Trails", hiveCat, "VENOM mid-way: every repeat climbs another fifth - glittering ascending trails.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 55 }, { rbTracking, 75 }, { rbMagic, 45 }, { rbTone, 55 } } },
        { "Descending Spiral", hiveCat, "Negative PITCH with VENOM: notes fall away in a spiral of fourths.",
          { { rbOn, 1 }, { rbPitch, -5 }, { rbPrimary, 65 }, { rbTracking, 70 }, { rbMagic, 60 }, { rbTone, 40 } } },
        { "Drowning Hive", hiveCat, "Atonal down-shift, loose tracking and heavy VENOM through the deep SWARM: moaning, gurgling.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -1.7f }, { rbPrimary, 70 }, { rbSecondary, 30 }, { rbTracking, 35 },
            { rbMagic, 80 }, { rbTone, 30 }, { swarmOn, 1 }, { swarmDeep, 1 }, { swarmMix, 40 } } },
        { "Venom Overload", hiveCat, "VENOM on the edge. Hold the VENOM footswitch: it takes off into squalls and drags +1 OCT in (LINK).",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 50 }, { rbTracking, 60 }, { rbMagic, 92 }, { rbTone, 60 }, { linkOct1, 1 } } },

        // ------------------------------------------------------------- textures
        { "Swarm Cloud", texture, "DEEP SWARM over a barely-detuned double: wide, seasick, huge.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, 0.2f }, { rbPrimary, 40 }, { rbTracking, 90 },
            { swarmOn, 1 }, { swarmDeep, 1 }, { swarmDepth, 75 }, { swarmRate, 0.35f }, { swarmMix, 60 } } },
        { "Smoked Out", texture, "SMOKE with GATE: the notes sputter and tear apart as they decay.",
          { { fuzzOn, 1 }, { fuzz, 85 }, { fuzzGate, 75 }, { fuzzTone, 45 } } },
        { "Wing Beat Breakdown", texture, "Tempo-synced 1/16 hard WINGS gate on SMOKE. Hold +1 OCT for angry stabs.",
          { { rise, 0 }, { panic, 30 }, { fuzzOn, 1 }, { fuzz, 65 }, { flowOn, 1 }, { flowSync, 1 }, { flowDiv, 4 }, { flowHard, 1 } } },
        { "Ghost Swarm", texture, "Smooth WINGS tremolo, a quiet octave-up DRONE with trails and SWARM - eerie clean parts.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 30 }, { rbMagic, 25 }, { rbTracking, 85 }, { rbTone, 45 },
            { swarmOn, 1 }, { swarmMix, 35 }, { flowOn, 1 }, { flowHard, 0 }, { flowSpeed, 5.5f }, { flowAmount, 70 } } },

        // ---------------------------------------------------------- swarm attack
        { "Queen Scream", attack, "Hot SMOKE into an angry, frenzied octave. Hold +2 OCT for the scream.",
          { { rise, 15 }, { panic, 50 }, { chaos, 15 }, { speed, 20 }, { fuzzOn, 1 }, { fuzz, 80 }, { fuzzTone, 65 } } },
        { "Broken Radio", attack, "Atonal loose HIVE, dark SMOKE after it and WINGS tremolo: a dying transmission.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -2.6f }, { rbPrimary, 80 }, { rbTracking, 15 }, { rbTone, 30 },
            { fuzzOn, 1 }, { fuzzPost, 1 }, { fuzz, 45 }, { fuzzTone, 20 },
            { flowOn, 1 }, { flowHard, 0 }, { flowSpeed, 6.0f }, { flowAmount, 60 } } },
        { "Hive Collapse", attack, "Everything at once. One stomp on VENOM: +2 OCT (LINK), max regeneration and gated SMOKE.",
          { { panic, 40 }, { chaos, 40 }, { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 60 }, { rbMagic, 100 }, { rbTracking, 55 },
            { fuzzOn, 1 }, { fuzzPost, 1 }, { fuzz, 90 }, { fuzzGate, 30 }, { linkOct2, 1 } } },
    };
}

juce::StringArray PresetManager::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& fp : factoryPresets)
        names.add (fp.name);
    return names;
}

juce::StringArray PresetManager::getFactoryCategories() const
{
    juce::StringArray cats;
    for (const auto& fp : factoryPresets)
        cats.addIfNotAlreadyThere (fp.category);
    return cats;
}

juce::StringArray PresetManager::getFactoryPresetNames (const juce::String& category) const
{
    juce::StringArray names;
    for (const auto& fp : factoryPresets)
        if (fp.category == category)
            names.add (fp.name);
    return names;
}

juce::String PresetManager::getPresetDescription (const juce::String& name) const
{
    for (const auto& fp : factoryPresets)
        if (fp.name == name)
            return fp.description;
    return {};
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getPresetsDirectory().findChildFiles (juce::File::findFiles, false, "*" + extension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::StringArray PresetManager::getAllPresetNames() const
{
    auto names = getFactoryPresetNames();
    for (const auto& n : getUserPresetNames())
        if (! names.contains (n))
            names.add (n);
    return names;
}

bool PresetManager::isFactoryPreset (const juce::String& name) const
{
    return getFactoryPresetNames().contains (name);
}

bool PresetManager::isUserPreset (const juce::String& name) const
{
    return ! isFactoryPreset (name) && getPresetsDirectory().getChildFile (name + extension).existsAsFile();
}

juce::File PresetManager::getPresetsDirectory()
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Swarmness");
   #elif JUCE_WINDOWS
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Swarmness/Presets");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".swarmness/presets");
   #endif

    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

//==============================================================================
PresetManager::ValueMap PresetManager::captureValues() const
{
    ValueMap values;
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (isPresetParameter (ranged->getParameterID()))
                values[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return values;
}

void PresetManager::applyValues (const ValueMap& values, bool resetOthersToDefault)
{
    for (auto* param : apvts.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr || ! isPresetParameter (ranged->getParameterID()))
            continue;

        float normalised = -1.0f;
        if (auto it = values.find (ranged->getParameterID()); it != values.end())
            normalised = ranged->convertTo0to1 (it->second);
        else if (resetOthersToDefault)
            normalised = ranged->getDefaultValue();

        if (normalised >= 0.0f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
            ranged->endChangeGesture();
        }
    }
}

void PresetManager::takeSnapshot()
{
    std::map<juce::String, float> values;
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (isPresetParameter (ranged->getParameterID()))
                values[ranged->getParameterID()] = ranged->getValue();

    const juce::ScopedLock sl (lock);
    snapshot = std::move (values);
}

juce::String PresetManager::getCurrentPresetName() const
{
    const juce::ScopedLock sl (lock);
    return currentName;
}

void PresetManager::setCurrentName (const juce::String& name)
{
    const juce::ScopedLock sl (lock);
    currentName = name;
}

bool PresetManager::isDirty() const
{
    const juce::ScopedLock sl (lock);
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (auto it = snapshot.find (ranged->getParameterID()); it != snapshot.end())
                if (std::abs (it->second - ranged->getValue()) > 1.0e-4f)
                    return true;
    return false;
}

void PresetManager::restoreFromState (const juce::String& presetName)
{
    setCurrentName (presetName.isNotEmpty() ? presetName : juce::String ("Init"));
    takeSnapshot();
}

//==============================================================================
juce::var PresetManager::toJson (const juce::String& name, const ValueMap& values)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    root->setProperty ("plugin", "Swarmness");
    root->setProperty ("version", JucePlugin_VersionString);

    auto* params = new juce::DynamicObject();
    for (const auto& [id, value] : values)
        params->setProperty (id, value);
    root->setProperty ("parameters", juce::var (params));

    return juce::var (root);
}

bool PresetManager::fromJson (const juce::var& json, juce::String& name, ValueMap& values)
{
    auto* params = json["parameters"].getDynamicObject();
    if (params == nullptr)
        return false;

    name = json["name"].toString();
    for (const auto& prop : params->getProperties())
        if (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool())
            values[prop.name.toString()] = (float) (double) prop.value;
    return true;
}

bool PresetManager::loadPreset (const juce::String& name)
{
    for (const auto& fp : factoryPresets)
    {
        if (fp.name == name)
        {
            applyValues (fp.values, true);
            setCurrentName (name);
            takeSnapshot();
            return true;
        }
    }

    auto file = getPresetsDirectory().getChildFile (name + extension);
    if (! file.existsAsFile())
        return false;

    juce::String storedName;
    ValueMap values;
    if (! fromJson (juce::JSON::parse (file), storedName, values))
        return false;

    applyValues (values, true);
    setCurrentName (name);
    takeSnapshot();
    return true;
}

void PresetManager::loadNextPreset()
{
    const auto all = getAllPresetNames();
    if (all.isEmpty()) return;
    const int idx = all.indexOf (getCurrentPresetName());
    loadPreset (all[(idx + 1) % all.size()]);
}

void PresetManager::loadPreviousPreset()
{
    const auto all = getAllPresetNames();
    if (all.isEmpty()) return;
    const int idx = all.indexOf (getCurrentPresetName());
    loadPreset (all[idx <= 0 ? all.size() - 1 : idx - 1]);
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = sanitiseName (rawName);
    if (name.isEmpty() || isFactoryPreset (name))
        return false;

    auto file = getPresetsDirectory().getChildFile (name + extension);
    if (! file.replaceWithText (juce::JSON::toString (toJson (name, captureValues()))))
        return false;

    setCurrentName (name);
    takeSnapshot();
    return true;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    if (! isUserPreset (name))
        return false;

    const auto all = getAllPresetNames();
    const int idx = all.indexOf (name);

    if (! getPresetsDirectory().getChildFile (name + extension).deleteFile())
        return false;

    if (getCurrentPresetName() == name)
    {
        const auto remaining = getAllPresetNames();
        loadPreset (remaining[juce::jlimit (0, remaining.size() - 1, idx - 1)]);
    }
    return true;
}

bool PresetManager::importPreset (const juce::File& file)
{
    juce::String name;
    ValueMap values;
    if (! fromJson (juce::JSON::parse (file), name, values))
        return false;

    if (name.isEmpty())
        name = file.getFileNameWithoutExtension();
    name = sanitiseName (name);
    if (isFactoryPreset (name))
        name << " (imported)";

    applyValues (values, true);
    return saveUserPreset (name);
}

bool PresetManager::exportPreset (const juce::File& file) const
{
    return file.replaceWithText (juce::JSON::toString (toJson (file.getFileNameWithoutExtension(), captureValues())));
}
