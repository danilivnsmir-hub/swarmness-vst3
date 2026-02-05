#pragma once
#include <JuceHeader.h>

class PresetManager {
public:
    PresetManager(juce::AudioProcessorValueTreeState& apvts);
    ~PresetManager() = default;

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
    
    static const juce::String kPresetExtension;
    static const juce::String kPresetVersion;

private:
    juce::var createPresetObject(const juce::String& name) const;
    void applyPresetFromVar(const juce::var& presetData);
    void initializeFactoryPresets();
    
    juce::AudioProcessorValueTreeState& mAPVTS;
    juce::String mCurrentPresetName{"Init"};
    
    std::map<juce::String, juce::var> mFactoryPresets;
};
