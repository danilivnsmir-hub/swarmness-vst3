#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/MetalLookAndFeel.h"
#include "GUI/RotaryKnob.h"
#include "GUI/FootswitchButton.h"
#include "GUI/PresetPanel.h"
#include "GUI/InfoPanel.h"
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

    // Background and Logo images
    juce::Image backgroundImage;
    juce::Image logoImage;

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

    // === PITCH Section ===
    juce::ComboBox octaveModeBox;
    RotaryKnob pitchRangeKnob{"RANGE"};
    RotaryKnob pitchSpeedKnob{"SPEED"};
    juce::Slider riseFader;  // Vertical fader for RISE
    juce::Label riseFaderLabel;
    juce::Label riseFaderValueLabel;
    juce::ToggleButton pitchOnButton{"ON/OFF"};

    // === MODULATION Section ===
    RotaryKnob angerKnob{"ANGER"};
    RotaryKnob rushKnob{"RUSH"};
    RotaryKnob modRateKnob{"RATE"};

    // === TONE Section ===
    RotaryKnob lowCutKnob{"LOW CUT"};
    RotaryKnob highCutKnob{"HIGH CUT"};
    RotaryKnob midBoostKnob{"MID BOOST"};

    // === SWARM Section ===
    RotaryKnob swarmDepthKnob{"DEPTH"};
    RotaryKnob swarmRateKnob{"RATE"};
    RotaryKnob swarmMixKnob{"MIX"};
    juce::ToggleButton chorusModeButton{"DEEP"};

    // === FLOW Section ===
    RotaryKnob flowAmountKnob{"AMOUNT"};
    RotaryKnob flowSpeedKnob{"SPEED"};
    juce::ToggleButton flowModeButton{"HARD"};

    // === OUTPUT Section ===
    RotaryKnob mixKnob{"MIX"};
    RotaryKnob volumeKnob{"VOLUME"};
    RotaryKnob driveKnob{"DRIVE"};

    // === Footswitch (Bottom Center) ===
    FootswitchButton bypassFootswitch;

    // Section Labels
    juce::Label pitchSectionLabel;
    juce::Label modulationSectionLabel;
    juce::Label toneSectionLabel;
    juce::Label swarmSectionLabel;
    juce::Label flowSectionLabel;
    juce::Label outputSectionLabel;

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
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chorusModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> saturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flowModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowSpeedAttachment;

    void setupSectionLabel(juce::Label& label, const juce::String& text);
    void drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    void refreshPresetList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
