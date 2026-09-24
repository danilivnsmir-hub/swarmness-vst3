#include "PresetManager.h"
#include "../Parameters.h"

const juce::String PresetManager::extension = ".swpreset";

namespace
{
    bool isPresetParameter (const juce::String& id)
    {
        return id != ParamIDs::bypass;
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

    // Octave choice indices: 0 = -2, 1 = -1, 2 = 0, 3 = +1, 4 = +2
    // Quality: 0 = Live, 1 = Studio.  Flow division: 3 = 1/8, 4 = 1/16
    factoryPresets = {
        { "Init", {} },
        { "Octave Up Classic",  { { octave, 3 }, { mix, 50 }, { rise, 20 } } },
        { "Sub Octave Djent",   { { octave, 1 }, { mix, 55 }, { lowCut, 30 }, { highCut, 6500 }, { mid, 2.0f } } },
        { "Whammy Rise",        { { octave, 3 }, { rise, 650 }, { mix, 100 }, { highCut, 12000 } } },
        { "Dive Bomb",          { { octave, 0 }, { rise, 1400 }, { mix, 100 }, { lowCut, 25 } } },
        { "Fifth Harmony",      { { octave, 2 }, { semitone, 7 }, { mix, 45 }, { swarmMix, 25 }, { swarmDepth, 40 } } },
        { "Swarm Cloud",        { { octave, 3 }, { rush, 16 }, { modRate, 0.7f }, { swarmDeep, 1 }, { swarmMix, 60 },
                                  { swarmDepth, 70 }, { swarmRate, 0.35f }, { mix, 45 }, { highCut, 9000 } } },
        { "Glitch Anger",       { { octave, 3 }, { anger, 70 }, { modRate, 4.0f }, { mix, 70 }, { lowCut, 90 } } },
        { "Panic Room",         { { octave, 1 }, { rush, 60 }, { modRate, 3.0f }, { drive, 30 }, { mix, 60 }, { mid, 3.0f } } },
        { "Random Arps",        { { octave, 3 }, { randRange, 12 }, { randSpeed, 6.0f }, { mix, 60 }, { swarmMix, 20 } } },
        { "Stutter 1/16",       { { octave, 3 }, { mix, 50 }, { flowAmount, 100 }, { flowSync, 1 }, { flowDiv, 4 }, { flowHard, 1 } } },
        { "Tremolo Ghost",      { { octave, 4 }, { mix, 35 }, { flowAmount, 60 }, { flowHard, 0 }, { flowSpeed, 5.0f },
                                  { swarmMix, 40 }, { highCut, 8000 } } },
        { "Fuzz Octaver",       { { octave, 3 }, { quality, 0 }, { drive, 70 }, { mid, 6.0f }, { lowCut, 120 }, { mix, 65 },
                                  { output, -4.0f } } },
        { "Live Octave Down",   { { octave, 1 }, { quality, 0 }, { mix, 60 }, { lowCut, 35 } } },
        { "Detune Wall",        { { octave, 2 }, { rush, 8 }, { modRate, 0.4f }, { swarmDeep, 1 }, { swarmMix, 50 },
                                  { swarmDepth, 55 }, { mix, 50 } } },
    };
}

juce::StringArray PresetManager::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& fp : factoryPresets)
        names.add (fp.first);
    return names;
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
    for (const auto& [presetName, values] : factoryPresets)
    {
        if (presetName == name)
        {
            applyValues (values, true);
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
