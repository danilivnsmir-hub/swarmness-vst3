#pragma once
#include <JuceHeader.h>
#include "../Preset/PresetManager.h"

class PresetPanel : public juce::Component,
                    public juce::ComboBox::Listener,
                    public juce::Button::Listener {
public:
    PresetPanel(PresetManager& presetManager);
    ~PresetPanel() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    void buttonClicked(juce::Button* button) override;

    void refreshPresetList();

private:
    PresetManager& mPresetManager;

    juce::ComboBox mPresetComboBox;
    juce::TextButton mSaveButton{"Save"};
    juce::TextButton mExportButton{"Export"};
    juce::TextButton mImportButton{"Import"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetPanel)
};
