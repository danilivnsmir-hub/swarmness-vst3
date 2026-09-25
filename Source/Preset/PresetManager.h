#pragma once

#include <JuceHeader.h>
#include <map>

/**
 * Factory + user preset handling.
 *
 * Presets are JSON files (.swpreset) that store parameter values in real units, keyed by
 * parameter ID. Unknown IDs are ignored and missing IDs fall back to defaults, so presets
 * remain compatible across versions. Loading/saving happens on the message thread;
 * the name/snapshot accessors are thread-safe.
 */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& apvts);

    static const juce::String extension;

    juce::StringArray getFactoryPresetNames() const;
    juce::StringArray getFactoryCategories() const;                          // in display order
    juce::StringArray getFactoryPresetNames (const juce::String& category) const;
    juce::String getPresetDescription (const juce::String& name) const;     // empty for user presets
    juce::StringArray getUserPresetNames() const;
    juce::StringArray getAllPresetNames() const;   // factory first, then user

    bool isFactoryPreset (const juce::String& name) const;
    bool isUserPreset (const juce::String& name) const;

    bool loadPreset (const juce::String& name);
    /** Step through one bank (factory or user). From outside that bank, lands on its first/last preset. */
    void loadNextPreset (bool userBank);
    void loadPreviousPreset (bool userBank);

    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    bool importPreset (const juce::File& file);
    bool exportPreset (const juce::File& file) const;

    juce::String getCurrentPresetName() const;
    bool isDirty() const;

    /** Called after the host restores plug-in state. */
    void restoreFromState (const juce::String& presetName);

    static juce::File getPresetsDirectory();

private:
    using ValueMap = std::map<juce::String, float>;

    void initialiseFactoryPresets();
    void applyValues (const ValueMap& values, bool resetOthersToDefault);
    ValueMap captureValues() const;
    void takeSnapshot();
    void setCurrentName (const juce::String&);
    void stepPreset (bool userBank, int delta);

    static juce::var toJson (const juce::String& name, const ValueMap& values);
    static bool fromJson (const juce::var& json, juce::String& name, ValueMap& values);

    juce::AudioProcessorValueTreeState& apvts;
    struct FactoryPreset
    {
        juce::String name, category, description;
        ValueMap values;
    };
    std::vector<FactoryPreset> factoryPresets;
    // Guarded by 'lock': the host may restore state from a non-message thread.
    mutable juce::CriticalSection lock;
    juce::String currentName { "Init" };
    std::map<juce::String, float> snapshot;   // normalised values at last load/save
};
