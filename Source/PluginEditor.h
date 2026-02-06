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
    
    // Info Panel
    InfoPanel infoPanel;
    juce::TextButton infoButton{"i"};

    // === VOLTAGE Section (Top Left) ===
    RotaryKnob grainKnob{"GRAIN"};
    RotaryKnob pitchKnob{"PITCH"};
    RotaryKnob driftKnob{"DRIFT"};
    juce::ComboBox octaveModeBox;

    // === PULSE Section (Top Center) ===
    RotaryKnob slideTimeKnob{"SLIDE TIME"};
    RotaryKnob slideRangeKnob{"SLIDE RANGE"};
    juce::ToggleButton slideOnButton{"SLIDE ON/OFF"};

    // === HIVE FILTER Section (Top Right) ===
    RotaryKnob cutoffKnob{"CUTOFF"};
    RotaryKnob resonanceKnob{"RESONANCE"};
    RotaryKnob midBoostKnob{"MID BOOST"};

    // === SWARM Section (Bottom Left) ===
    RotaryKnob chorusDepthKnob{"CHORUS\nDEPTH"};
    RotaryKnob chorusRateKnob{"CHORUS\nRATE"};
    RotaryKnob wowFlutterKnob{"WOW/\nFLUTTER"};

    // === FLOW Section (Bottom Center) ===
    RotaryKnob flowAmountKnob{"FLOW\nAMOUNT"};
    RotaryKnob flowSpeedKnob{"FLOW\nSPEED"};
    juce::ToggleButton flowModeButton{"FLOW MODE"};

    // === OUTPUT Section (Bottom Right) ===
    RotaryKnob mixKnob{"MIX"};
    RotaryKnob driveKnob{"DRIVE"};
    RotaryKnob outputLevelKnob{"OUTPUT\nLEVEL"};

    // === Footswitch (Bottom Center) ===
    FootswitchButton bypassFootswitch;

    // Hidden controls (still connected to processor)
    juce::ToggleButton engageButton{"ENG"};
    RotaryKnob slidePositionKnob{"POS"};
    juce::ComboBox slideDirectionBox;
    juce::ToggleButton autoSlideButton{"AUTO"};
    juce::ToggleButton slideReturnButton{"RTN"};
    RotaryKnob randomRangeKnob{"RANGE"};
    RotaryKnob randomRateKnob{"RATE"};
    RotaryKnob randomSmoothKnob{"SMOOTH"};
    juce::ComboBox randomModeBox;
    juce::ComboBox chorusModeBox;
    RotaryKnob chorusMixKnob{"CHMIX"};
    juce::ComboBox flowModeBox;
    FootswitchButton pulseFootswitch;
    RotaryKnob speedKnob{"SPEED"};
    RotaryKnob highCutKnob{"HIGH CUT"};

    // Section Labels
    juce::Label voltageSectionLabel;
    juce::Label pulseSectionLabel;
    juce::Label hiveFilterSectionLabel;
    juce::Label swarmSectionLabel;
    juce::Label flowSectionLabel;
    juce::Label outputSectionLabel;

    // Parameter Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> octaveModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> engageAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> riseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slideRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slideTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slidePositionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slideDirectionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoSlideAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> slideReturnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomSmoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> randomModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panicAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chaosAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chorusModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> saturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> flowModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowSpeedAttachment;

    void setupSectionLabel(juce::Label& label, const juce::String& text);
    void drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
