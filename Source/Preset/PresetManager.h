#pragma once
#include <JuceHeader.h>

class PresetManager : public juce::AudioProcessorValueTreeState::Listener {
public:
    PresetManager(juce::AudioProcessorValueTreeState& apvts);
    ~PresetManager() override;

    void savePreset(const juce::String& name);
    void loadPreset(const juce::String& name);
    void loadPresetFromFile(const juce::File& file);
    bool exportPreset(const juce::File& file);
    bool importPreset(const juce::File& file);
    void deletePreset(const juce::String& name);
    
    juce::StringArray getPresetList() const;
    juce::String getCurrentPresetName() const { return mCurrentPresetName; }
    void setCurrentPresetName(const juce::String& name) { mCurrentPresetName = name; }

    juce::File getPresetsDirectory() const;
    juce::StringArray getFactoryPresetNames() const;
    void loadFactoryPreset(const juce::String& name);
    
    // Dirty state management
    bool isDirty() const { return mIsDirty; }
    void markDirty();
    void markClean();
    void saveSnapshot();
    bool hasChangedFromSnapshot() const;
    void initializeDirtyTracking();  // Call after processor is fully constructed
    
    // Listener callback
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    
    static const juce::String kPresetExtension;
    static const juce::String kPresetVersion;

private:
    juce::var createPresetObject(const juce::String& name) const;
    void applyPresetFromVar(const juce::var& presetData);
    void initializeFactoryPresets();
    void registerParameterListeners();
    void unregisterParameterListeners();
    
    juce::AudioProcessorValueTreeState& mAPVTS;
    juce::String mCurrentPresetName{"Init"};
    
    std::map<juce::String, juce::var> mFactoryPresets;
    
    // Dirty state tracking
    bool mIsDirty = false;
    bool mListenersRegistered = false;
    std::map<juce::String, float> mSnapshot;
};
