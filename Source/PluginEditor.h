#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/MetalLookAndFeel.h"
#include "GUI/RotaryKnob.h"
#include "GUI/FootswitchButton.h"
#include "GUI/PresetPanel.h"
#include "GUI/InfoPanel.h"
#include "GUI/PowerButton.h"
#include "BinaryData.h"

class SwarmnesssAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::Timer {
public:
    SwarmnesssAudioProcessorEditor(SwarmnesssAudioProcessor&);
    ~SwarmnesssAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SwarmnesssAudioProcessor& audioProcessor;
    MetalLookAndFeel metalLookAndFeel;

    // Background image and header logo
    juce::Image backgroundImage;
    juce::Image headerLogoImage;

    // Preset Panel
    std::unique_ptr<PresetPanel> presetPanel;
    
    // Preset Dropdown (simplified UI)
    juce::ComboBox presetSelector;
    
    // Preset buttons
    juce::TextButton savePresetButton{"SAVE"};
    juce::TextButton exportPresetButton{"EXPORT"};
    juce::TextButton importPresetButton{"IMPORT"};
    
    // Info Panel
    InfoPanel infoPanel;
    juce::TextButton infoButton{"i"};

    // === PITCH + MODULATION Combined Section ===
    // Power button for entire PITCH+MOD section
    PowerButton pitchModPowerButton;
    
    // PITCH controls
    juce::ComboBox octaveModeBox;
    RotaryKnob pitchRangeKnob{"RANGE"};
    RotaryKnob pitchSpeedKnob{"SPEED"};
    juce::Slider riseFader;
    juce::Label riseFaderLabel;
    juce::Label riseFaderValueLabel;

    // MODULATION controls
    RotaryKnob angerKnob{"ANGER"};
    RotaryKnob rushKnob{"RUSH"};
    RotaryKnob modRateKnob{"RATE"};

    // === TONE Section (no power button) ===
    RotaryKnob lowCutKnob{"LOW CUT"};
    RotaryKnob highCutKnob{"HIGH CUT"};
    RotaryKnob midBoostKnob{"MID BOOST"};

    // === SWARM Section ===
    PowerButton swarmPowerButton;
    RotaryKnob swarmDepthKnob{"DEPTH"};
    RotaryKnob swarmRateKnob{"RATE"};
    RotaryKnob swarmMixKnob{"MIX"};
    juce::ToggleButton chorusModeButton;  // Toggle without built-in text
    juce::Label chorusModeLabel;  // "DEEP" label below toggle

    // === FLOW Section ===
    PowerButton flowPowerButton;
    RotaryKnob flowAmountKnob{"AMOUNT"};
    RotaryKnob flowSpeedKnob{"SPEED"};
    // flowModeButton removed - flowMode is always true (Hard)

    // === OUTPUT Section (no power button) ===
    RotaryKnob mixKnob{"MIX"};
    RotaryKnob volumeKnob{"VOLUME"};
    RotaryKnob driveKnob{"DRIVE"};

    // === Footswitch (Bottom Center) ===
    FootswitchButton bypassFootswitch;

    // Section Labels (for subheaders)
    juce::Label pitchSubLabel;
    juce::Label modulationSubLabel;

    // Parameter Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> octaveModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> engageAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> riseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panicAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chaosAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chorusEngageAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chorusModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> saturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flowEngageAttachment;
    // flowModeAttachment removed - flowMode is always true (Hard)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowSpeedAttachment;

    void drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    void drawCombinedSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds);
    void refreshPresetList();
    void updatePresetName();
    void updateSectionEnableStates();
    void setSectionEnabled(std::vector<juce::Component*> components, bool enabled);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
