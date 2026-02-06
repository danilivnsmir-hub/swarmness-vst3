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

    // === PITCH Section (Top Left) ===
    juce::ComboBox octaveModeBox;
    RotaryKnob pitchRangeKnob{"RANGE"};
    RotaryKnob pitchSpeedKnob{"SPEED"};
    juce::ToggleButton pitchOnButton{"ON"};

    // === MODULATION Section (Top Center) ===
    RotaryKnob angerKnob{"ANGER"};
    RotaryKnob rushKnob{"RUSH"};
    RotaryKnob modRateKnob{"RATE"};

    // === TONE Section (Top Right) ===
    RotaryKnob lowCutKnob{"LOW CUT"};
    RotaryKnob highCutKnob{"HIGH CUT"};
    RotaryKnob midBoostKnob{"MID BOOST"};

    // === SWARM Section (Bottom Left - wider) ===
    RotaryKnob chorusDepthKnob{"DEPTH"};
    RotaryKnob chorusRateKnob{"RATE"};
    RotaryKnob chorusMixKnob{"MIX"};

    // === OUTPUT Section (Bottom Right - wider) ===
    RotaryKnob mixKnob{"MIX"};
    RotaryKnob volumeKnob{"VOLUME"};

    // === Footswitch (Bottom Center) ===
    FootswitchButton bypassFootswitch;

    // Hidden controls (still connected to processor for preset compatibility)
    juce::ToggleButton engageButton{"ENG"};
    RotaryKnob slidePositionKnob{"POS"};
    juce::ComboBox slideDirectionBox;
    juce::ToggleButton autoSlideButton{"AUTO"};
    juce::ToggleButton slideReturnButton{"RTN"};
    RotaryKnob randomRangeKnob{"RRANGE"};
    RotaryKnob randomRateKnob{"RRATE"};
    RotaryKnob randomSmoothKnob{"SMOOTH"};
    juce::ComboBox randomModeBox;
    juce::ComboBox chorusModeBox;
    juce::ComboBox flowModeBox;
    FootswitchButton pulseFootswitch;
    RotaryKnob driveKnob{"DRIVE"};
    RotaryKnob flowAmountKnob{"FLOW"};
    RotaryKnob flowSpeedKnob{"FSPD"};
    RotaryKnob saturationKnob{"SAT"};
    RotaryKnob slideTimeKnob{"STIME"};
    RotaryKnob slideRangeKnob{"SRANGE"};

    // Section Labels
    juce::Label pitchSectionLabel;
    juce::Label modulationSectionLabel;
    juce::Label toneSectionLabel;
    juce::Label swarmSectionLabel;
    juce::Label outputSectionLabel;

    // Parameter Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> octaveModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pitchOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> angerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rushAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    
    // Hidden parameter attachments for compatibility
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> engageAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slidePositionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slideDirectionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoSlideAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> slideReturnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> randomSmoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> randomModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chorusModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> flowModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flowSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> saturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slideTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slideRangeAttachment;

    void setupSectionLabel(juce::Label& label, const juce::String& text);
    void drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    void refreshPresetList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmnesssAudioProcessorEditor)
};
