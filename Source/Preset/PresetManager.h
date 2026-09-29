#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <array>

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
    bool exportPreset (const juce::File& file);

    juce::String getCurrentPresetName() const;
    bool isDirty() const;

    /** Called after the host restores plug-in state. */
    void restoreFromState (const juce::String& presetName);

    //==============================================================================
    /**
     * Scenes: every preset holds up to four versions of its sound (A..D). Edits belong to the
     * scene you are in; a scene you enter for the first time starts as a copy of the one you
     * came from. The chain order / routing is shared by all scenes. Message thread, except that
     * hosts may save the session (scenesToVar) from any thread: the scenes are guarded by `lock`.
     */
    static constexpr int kScenes = 4;
    void selectScene (int index);
    int getCurrentScene() const { const juce::ScopedLock sl (lock); return currentScene; }
    bool isSceneUsed (int index) const;
    /** Copies the scene you are in over another one. */
    void copyCurrentSceneTo (int index);
    /** Session state: the scenes (with the current one captured). */
    juce::var scenesToVar();
    void scenesFromVar (const juce::var&, int current);

    static juce::File getPresetsDirectory();
    /** Moves presets of the first Swarmness versions (unreadable) out of the user bank; returns how many. */
    static int moveUnsupportedPresets (const juce::File& dir);

    using ValueMap = std::map<juce::String, float>;

    /** Brings values saved by older versions up to date (also used for restored sessions). */
    static void migrateLegacyValues (ValueMap& values);

    /** Writes one of the ready-made TRAILS step patterns (HiveBlock::Fill) into a value map. */
    static void writeTrailFill (ValueMap& values, int fill);

    /** Non-parameter data stored with user presets (the CRYPT impulse response path). */
    std::function<void (juce::DynamicObject&)> onSaveExtras;
    std::function<void (const juce::var&)> onLoadExtras;

private:

    void initialiseFactoryPresets();
    void applyValues (const ValueMap& values, bool resetOthersToDefault);
    ValueMap captureValues() const;
    void takeSnapshot();
    void setCurrentName (const juce::String&);
    void stepPreset (bool userBank, int delta);

    juce::var toJson (const juce::String& name, const ValueMap& values) const;
    /** The whole preset as JSON: scene A as its parameters, the other scenes alongside. */
    juce::var presetJson (const juce::String& name);
    void loadScenesFromJson (const juce::var& json);
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

    static bool isSceneParameter (const juce::String& id);
    ValueMap captureSceneValues() const;
    void resetScenes();
    void syncSceneSnapshot();
    std::array<ValueMap, kScenes> scenes;       // empty = not created yet
    std::array<ValueMap, kScenes> savedScenes;  // as loaded / saved (dirty tracking)
    int currentScene = 0;
};
