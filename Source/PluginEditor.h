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

    // v1.2.4: Grid constants
    static constexpr int GRID = 8;
    static constexpr int PADDING = 12;
    static constexpr int GAP = 24;
    static constexpr int HEADER_HEIGHT = 56;  // 7 * GRID
    static constexpr int SIDE_PANEL_WIDTH = 64;  // 8 * GRID
    static constexpr int KNOB_SIZE = 56;  // 7 * GRID
    static constexpr int SLIDER_HEIGHT = 24;  // 3 * GRID

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
    
    // v1.2.7: New preset buttons layout
    juce::TextButton prevPresetButton{"<"};
    juce::TextButton nextPresetButton{">"};
    juce::TextButton savePresetButton{"SAVE"};
    juce::TextButton saveAsButton{"SAVE AS"};
    juce::TextButton deletePresetButton{"DELETE"};
    
    // Info Panel
    InfoPanel infoPanel;
    juce::TextButton infoButton{"i"};

    // === LEFT PANEL: TONE (vertical faders) ===
    juce::Slider lowCutFader;
    juce::Slider highCutFader;
    juce::Slider midBoostFader;
    juce::Label lowCutLabel, highCutLabel, midBoostLabel;
    juce::Label lowCutValueLabel, highCutValueLabel, midBoostValueLabel;

    // === RIGHT PANEL: OUTPUT (vertical faders) ===
    juce::Slider mixFader;
    juce::Slider driveFader;
    juce::Slider volumeFader;
    juce::Label mixLabel, driveLabel, volumeLabel;
    juce::Label mixValueLabel, driveValueLabel, volumeValueLabel;

    // === CENTER TOP: VOLTAGE Section ===
    PowerButton pitchBypassButton;  // Power toggle for VOLTAGE
    juce::Label voltageLabel;  // Main VOLTAGE header
    juce::ComboBox octaveModeBox;
    RotaryKnob pitchRangeKnob{"RANGE"};
    RotaryKnob pitchSpeedKnob{"SPEED"};
    
    // v1.2.4: RISE as horizontal slider
    juce::Slider riseSlider;
    juce::Label riseLabel;
    juce::Label riseValueLabel;
    
    RotaryKnob angerKnob{"ANGER"};
    RotaryKnob rushKnob{"RUSH"};
    RotaryKnob modRateKnob{"RATE"};
    
    // PITCH / MODULATION subheaders
    juce::Label pitchSubLabel;
    juce::Label modulationSubLabel;

    // === CENTER BOTTOM LEFT: SWARM Section ===
    PowerButton swarmBypassButton;
    juce::Label swarmLabel;
    RotaryKnob swarmDepthKnob{"DEPTH"};
    RotaryKnob swarmRateKnob{"RATE"};
    RotaryKnob swarmMixKnob{"MIX"};
    juce::ToggleButton deepModeButton;
    juce::Label deepModeLabel;

    // === CENTER BOTTOM RIGHT: FLOW Section ===
    PowerButton flowBypassButton;
    juce::Label flowLabel;
    RotaryKnob flowAmountKnob{"AMOUNT"};
    RotaryKnob flowSpeedKnob{"SPEED"};

    // === BYPASS Footswitch (Bottom Center) ===
    FootswitchButton bypassFootswitch;

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
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowSpeedAttachment;

    void refreshPresetList();
    void updatePresetName();
    void updateSectionEnableStates();
    void updateDeleteButtonVisibility();  // v1.2.6: Show/hide delete button based on preset type
    void setSectionEnabled(std::vector<juce::Component*> components, bool enabled);
    enum class FaderScaleMode { Percent, Scale1to10_Step01, Scale1to10_Step05, LowCutHz, HighCutHz, Custom };
    void setupVerticalFader(juce::Slider& fader, juce::Label& label, juce::Label& valueLabel, 
                            const juce::String& labelText, FaderScaleMode scaleMode = FaderScaleMode::Percent);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
