#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/MetalLookAndFeel.h"
#include "GUI/RotaryKnob.h"
#include "GUI/FootswitchButton.h"
#include "GUI/PresetPanel.h"
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

    // === VOLTAGE Section (Top Left) ===
    // Maps to PITCH controls - renamed per prototype
    RotaryKnob grainKnob{"GRAIN"};      // panic
    RotaryKnob pitchKnob{"PITCH"};      // chaos (repurposed)
    RotaryKnob driftKnob{"DRIFT"};      // rise
    juce::ComboBox octaveModeBox;

    // === PULSE Section (Top Center) ===
    // Maps to SLIDE controls
    RotaryKnob slideTimeKnob{"SLIDE TIME"};
    RotaryKnob slideRangeKnob{"SLIDE RANGE"};
    juce::ToggleButton slideOnButton{"SLIDE ON/OFF"};

    // === HIVE FILTER Section (Top Right) ===
    // Filter controls
    RotaryKnob cutoffKnob{"CUTOFF"};        // lowCut
    RotaryKnob resonanceKnob{"RESONANCE"};  // highCut repurposed
    RotaryKnob midBoostKnob{"MID BOOST"};   // New or repurposed

    // === SWARM Section (Bottom Left) ===
    // Chorus controls
    RotaryKnob chorusDepthKnob{"CHORUS\nDEPTH"};
    RotaryKnob chorusRateKnob{"CHORUS\nRATE"};
    RotaryKnob wowFlutterKnob{"WOW/\nFLUTTER"};

    // === FLOW Section (Bottom Center) ===
    RotaryKnob flowAmountKnob{"FLOW AMOUNT"};
    RotaryKnob flowSpeedKnob{"FLOW SPEED"};
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
    RotaryKnob pulseRateKnob{"RATE"};
    RotaryKnob pulseProbabilityKnob{"PROB"};
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
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseProbabilityAttachment;

    void setupSectionLabel(juce::Label& label, const juce::String& text);
    void drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
