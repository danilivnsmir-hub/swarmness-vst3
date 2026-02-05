#include "PresetPanel.h"
#include "MetalLookAndFeel.h"

PresetPanel::PresetPanel(PresetManager& presetManager)
    : mPresetManager(presetManager)
{
    addAndMakeVisible(mPresetComboBox);
    mPresetComboBox.addListener(this);
    mPresetComboBox.setTextWhenNothingSelected("Select Preset");

    addAndMakeVisible(mSaveButton);
    mSaveButton.addListener(this);

    addAndMakeVisible(mExportButton);
    mExportButton.addListener(this);

    addAndMakeVisible(mImportButton);
    mImportButton.addListener(this);

    refreshPresetList();
}

void PresetPanel::resized() {
    auto bounds = getLocalBounds().reduced(10, 5);
    auto buttonWidth = 70;
    auto spacing = 8;

    mImportButton.setBounds(bounds.removeFromRight(buttonWidth));
    bounds.removeFromRight(spacing);
    mExportButton.setBounds(bounds.removeFromRight(buttonWidth));
    bounds.removeFromRight(spacing);
    mSaveButton.setBounds(bounds.removeFromRight(buttonWidth));
    bounds.removeFromRight(spacing);
    
    mPresetComboBox.setBounds(bounds);
}

void PresetPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    
    // Background
    g.setColour(MetalLookAndFeel::getMetalGrey().darker(0.3f));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Border
    g.setColour(MetalLookAndFeel::getMetalLight());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
}

void PresetPanel::comboBoxChanged(juce::ComboBox* comboBox) {
    if (comboBox == &mPresetComboBox) {
        auto presetName = mPresetComboBox.getText();
        if (presetName.isNotEmpty()) {
            mPresetManager.loadPreset(presetName);
        }
    }
}

void PresetPanel::buttonClicked(juce::Button* button) {
    if (button == &mSaveButton) {
        auto presetName = mPresetComboBox.getText();
        if (presetName.isEmpty()) {
            presetName = "New Preset";
        }
        
        // Show dialog to get preset name
        auto* window = new juce::AlertWindow("Save Preset", "Enter preset name:",
                                              juce::AlertWindow::QuestionIcon);
        window->addTextEditor("name", presetName);
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        
        window->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, window](int result) {
                if (result == 1) {
                    auto name = window->getTextEditorContents("name");
                    if (name.isNotEmpty()) {
                        mPresetManager.savePreset(name);
                        refreshPresetList();
                    }
                }
                delete window;
            }), true);
    }
    else if (button == &mExportButton) {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export Preset",
            mPresetManager.getPresetsDirectory(),
            "*" + PresetManager::kPresetExtension);

        chooser->launchAsync(juce::FileBrowserComponent::saveMode,
            [this, chooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{}) {
                    auto path = file.getFullPathName();
                    if (!path.endsWith(PresetManager::kPresetExtension))
                        path += PresetManager::kPresetExtension;
                    mPresetManager.exportPreset(juce::File(path));
                }
            });
    }
    else if (button == &mImportButton) {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Import Preset",
            mPresetManager.getPresetsDirectory(),
            "*" + PresetManager::kPresetExtension);

        chooser->launchAsync(juce::FileBrowserComponent::openMode,
            [this, chooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    mPresetManager.importPreset(file);
                    refreshPresetList();
                }
            });
    }
}

void PresetPanel::refreshPresetList() {
    mPresetComboBox.clear();
    auto presets = mPresetManager.getPresetList();
    
    int id = 1;
    for (const auto& preset : presets) {
        mPresetComboBox.addItem(preset, id++);
    }

    // Select current preset
    auto currentPreset = mPresetManager.getCurrentPresetName();
    auto index = presets.indexOf(currentPreset);
    if (index >= 0) {
        mPresetComboBox.setSelectedId(index + 1, juce::dontSendNotification);
    }
}
