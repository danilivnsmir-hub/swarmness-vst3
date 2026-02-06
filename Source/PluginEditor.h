#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/MetalLookAndFeel.h"
#include "GUI/RotaryKnob.h"
#include "GUI/FootswitchButton.h"
#include "GUI/PresetPanel.h"

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

    // Preset Panel
    std::unique_ptr<PresetPanel> presetPanel;

    // === VOLTAGE Section ===
    juce::ComboBox octaveModeBox;
    juce::ToggleButton engageButton{"ENG"};
    RotaryKnob riseKnob{"RISE"};

    // Slide
    RotaryKnob slideRangeKnob{"RANGE"};
    RotaryKnob slideTimeKnob{"TIME"};
    RotaryKnob slidePositionKnob{"POS"};
    juce::ComboBox slideDirectionBox;
    juce::ToggleButton autoSlideButton{"AUTO"};
    juce::ToggleButton slideReturnButton{"RTN"};

    // Random
    RotaryKnob randomRangeKnob{"RANGE"};
    RotaryKnob randomRateKnob{"RATE"};
    RotaryKnob randomSmoothKnob{"SMOOTH"};
    juce::ComboBox randomModeBox;

    // === MODULATION Section ===
    RotaryKnob panicKnob{"PANIC"};
    RotaryKnob chaosKnob{"CHAOS"};
    RotaryKnob speedKnob{"SPEED"};

    // === TONE SHAPING Section ===
    RotaryKnob lowCutKnob{"LOW CUT"};
    RotaryKnob highCutKnob{"HIGH CUT"};
    RotaryKnob chorusRateKnob{"RATE"};
    RotaryKnob chorusDepthKnob{"DEPTH"};
    RotaryKnob chorusMixKnob{"MIX"};
    RotaryKnob saturationKnob{"DRIVE"};

    // === OUTPUT Section ===
    RotaryKnob mixKnob{"MIX"};
    RotaryKnob outputGainKnob{"OUTPUT"};

    // === PULSE Section (was FLOW) ===
    juce::ComboBox flowModeBox;
    RotaryKnob pulseRateKnob{"RATE"};
    RotaryKnob pulseProbabilityKnob{"PROB"};
    FootswitchButton pulseFootswitch;
    FootswitchButton bypassFootswitch;

    // Labels for sections
    juce::Label pitchSectionLabel;   // was voltageSectionLabel
    juce::Label slideSectionLabel;
    juce::Label randomSectionLabel;
    juce::Label modulationSectionLabel;
    juce::Label toneSectionLabel;
    juce::Label chorusSectionLabel;
    juce::Label outputSectionLabel;
    juce::Label pulseSectionLabel;   // was flowSectionLabel

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
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> saturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> flowModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseProbabilityAttachment;

    void setupLabel(juce::Label& label, const juce::String& text, bool isSection = true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
