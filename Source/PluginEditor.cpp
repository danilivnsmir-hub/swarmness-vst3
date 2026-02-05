#include "PluginProcessor.h"
#include "PluginEditor.h"

SwarmnesssAudioProcessorEditor::SwarmnesssAudioProcessorEditor(SwarmnesssAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&metalLookAndFeel);
    setSize(950, 750);

    // Preset Panel
    presetPanel = std::make_unique<PresetPanel>(audioProcessor.getPresetManager());
    addAndMakeVisible(*presetPanel);

    // === Setup Labels ===
    setupLabel(voltageSectionLabel, "VOLTAGE");
    setupLabel(slideSectionLabel, "SLIDE", false);
    setupLabel(randomSectionLabel, "RANDOM", false);
    setupLabel(modulationSectionLabel, "MODULATION");
    setupLabel(toneSectionLabel, "TONE SHAPING");
    setupLabel(chorusSectionLabel, "CHORUS", false);
    setupLabel(outputSectionLabel, "OUTPUT");
    setupLabel(flowSectionLabel, "FLOW");

    // === VOLTAGE Section ===
    addAndMakeVisible(octaveModeBox);
    octaveModeBox.addItem("+1 OCT", 1);
    octaveModeBox.addItem("+2 OCT", 2);
    octaveModeBox.addItem("-1 OCT", 3);
    octaveModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "octaveMode", octaveModeBox);

    addAndMakeVisible(engageButton);
    engageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "engage", engageButton);

    addAndMakeVisible(riseKnob);
    riseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "rise", riseKnob.getSlider());

    // Slide
    addAndMakeVisible(slideRangeKnob);
    slideRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "slideRange", slideRangeKnob.getSlider());

    addAndMakeVisible(slideTimeKnob);
    slideTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "slideTime", slideTimeKnob.getSlider());

    addAndMakeVisible(slidePositionKnob);
    slidePositionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "slidePosition", slidePositionKnob.getSlider());

    addAndMakeVisible(slideDirectionBox);
    slideDirectionBox.addItem("Up", 1);
    slideDirectionBox.addItem("Down", 2);
    slideDirectionBox.addItem("Both", 3);
    slideDirectionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "slideDirection", slideDirectionBox);

    addAndMakeVisible(autoSlideButton);
    autoSlideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoSlide", autoSlideButton);

    addAndMakeVisible(slideReturnButton);
    slideReturnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "slideReturn", slideReturnButton);

    // Random
    addAndMakeVisible(randomRangeKnob);
    randomRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRange", randomRangeKnob.getSlider());

    addAndMakeVisible(randomRateKnob);
    randomRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRate", randomRateKnob.getSlider());

    addAndMakeVisible(randomSmoothKnob);
    randomSmoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomSmooth", randomSmoothKnob.getSlider());

    addAndMakeVisible(randomModeBox);
    randomModeBox.addItem("Jump", 1);
    randomModeBox.addItem("Glide", 2);
    randomModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "randomMode", randomModeBox);

    // === MODULATION Section ===
    addAndMakeVisible(panicKnob);
    panicAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "panic", panicKnob.getSlider());

    addAndMakeVisible(chaosKnob);
    chaosAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chaos", chaosKnob.getSlider());

    addAndMakeVisible(speedKnob);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "speed", speedKnob.getSlider());

    // === TONE SHAPING Section ===
    addAndMakeVisible(lowCutKnob);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowCut", lowCutKnob.getSlider());

    addAndMakeVisible(highCutKnob);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highCut", highCutKnob.getSlider());

    addAndMakeVisible(saturationKnob);
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", saturationKnob.getSlider());

    // Chorus
    addAndMakeVisible(chorusRateKnob);
    chorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusRate", chorusRateKnob.getSlider());

    addAndMakeVisible(chorusDepthKnob);
    chorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusDepth", chorusDepthKnob.getSlider());

    addAndMakeVisible(chorusMixKnob);
    chorusMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusMix", chorusMixKnob.getSlider());

    // === OUTPUT Section ===
    addAndMakeVisible(mixKnob);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixKnob.getSlider());

    addAndMakeVisible(outputGainKnob);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", outputGainKnob.getSlider());

    // === FLOW Section ===
    addAndMakeVisible(flowModeBox);
    flowModeBox.addItem("Static", 1);
    flowModeBox.addItem("Pulse", 2);
    flowModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "flowMode", flowModeBox);

    addAndMakeVisible(pulseRateKnob);
    pulseRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "pulseRate", pulseRateKnob.getSlider());

    addAndMakeVisible(pulseProbabilityKnob);
    pulseProbabilityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "pulseProbability", pulseProbabilityKnob.getSlider());

    addAndMakeVisible(footswitch);
    footswitch.onClick = [this](bool isOn) {
        audioProcessor.getFlowEngine().setStaticState(isOn);
    };

    // Start timer for LED updates
    startTimerHz(30);
}

SwarmnesssAudioProcessorEditor::~SwarmnesssAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SwarmnesssAudioProcessorEditor::setupLabel(juce::Label& label, const juce::String& text, bool isSection) {
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, 
        isSection ? MetalLookAndFeel::getAccentOrange() : MetalLookAndFeel::getTextDim());
    label.setFont(juce::Font(isSection ? 14.0f : 11.0f, isSection ? juce::Font::bold : juce::Font::plain));
    addAndMakeVisible(label);
}

void SwarmnesssAudioProcessorEditor::timerCallback() {
    // Update footswitch LED based on flow engine state
    auto flowMode = static_cast<int>(*audioProcessor.getAPVTS().getRawParameterValue("flowMode"));
    if (flowMode == 1) {  // Pulse mode
        footswitch.setLEDState(FootswitchButton::OrangeBlinking);
    } else {
        footswitch.setLEDState(audioProcessor.getFlowEngine().isCurrentlyOn() ? 
            FootswitchButton::Green : FootswitchButton::Red);
    }
}

void SwarmnesssAudioProcessorEditor::paint(juce::Graphics& g) {
    // Background
    g.fillAll(MetalLookAndFeel::getBackgroundDark());

    // Title
    g.setColour(MetalLookAndFeel::getTextLight());
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.drawText("SWARMNESS", 0, 50, getWidth(), 30, juce::Justification::centred);

    g.setColour(MetalLookAndFeel::getTextDim());
    g.setFont(juce::Font(12.0f));
    g.drawText("v2.1.0 • Tone Shaping Edition", 0, 78, getWidth(), 20, juce::Justification::centred);

    // Section backgrounds
    auto drawSectionBg = [&](juce::Rectangle<int> bounds, const juce::String& title = "") {
        g.setColour(MetalLookAndFeel::getMetalGrey().withAlpha(0.3f));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(MetalLookAndFeel::getMetalLight().withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };

    // VOLTAGE section
    drawSectionBg(juce::Rectangle<int>(15, 105, 430, 280));
    
    // MODULATION section
    drawSectionBg(juce::Rectangle<int>(455, 105, 220, 120));
    
    // TONE SHAPING section
    drawSectionBg(juce::Rectangle<int>(15, 395, 540, 170));
    
    // OUTPUT section
    drawSectionBg(juce::Rectangle<int>(565, 395, 170, 170));
    
    // FLOW section
    drawSectionBg(juce::Rectangle<int>(565, 235, 370, 150));

    // Footer
    g.setColour(MetalLookAndFeel::getTextDim().withAlpha(0.5f));
    g.setFont(juce::Font(10.0f));
    g.drawText("© 2026 OpenAudio • Pitch Shifter for Metalcore/Djent", 
               0, getHeight() - 25, getWidth(), 20, juce::Justification::centred);
}

void SwarmnesssAudioProcessorEditor::resized() {
    const int knobSize = 70;
    const int smallKnobSize = 60;
    const int comboHeight = 24;
    const int toggleHeight = 24;
    const int labelHeight = 20;
    const int margin = 10;

    // Preset Panel
    presetPanel->setBounds(15, 10, getWidth() - 30, 35);

    // === VOLTAGE Section ===
    int voltageX = 25;
    int voltageY = 110;
    
    voltageSectionLabel.setBounds(voltageX, voltageY, 100, labelHeight);
    
    // Core pitch controls
    octaveModeBox.setBounds(voltageX, voltageY + 25, 80, comboHeight);
    engageButton.setBounds(voltageX + 90, voltageY + 25, 50, toggleHeight);
    riseKnob.setBounds(voltageX + 150, voltageY + 15, knobSize, knobSize + 16);

    // SLIDE subsection
    slideSectionLabel.setBounds(voltageX, voltageY + 95, 60, labelHeight);
    
    slideRangeKnob.setBounds(voltageX, voltageY + 115, smallKnobSize, smallKnobSize + 16);
    slideTimeKnob.setBounds(voltageX + 65, voltageY + 115, smallKnobSize, smallKnobSize + 16);
    slidePositionKnob.setBounds(voltageX + 130, voltageY + 115, smallKnobSize, smallKnobSize + 16);
    
    slideDirectionBox.setBounds(voltageX + 200, voltageY + 115, 70, comboHeight);
    autoSlideButton.setBounds(voltageX + 200, voltageY + 145, 55, toggleHeight);
    slideReturnButton.setBounds(voltageX + 260, voltageY + 145, 50, toggleHeight);

    // RANDOM subsection
    randomSectionLabel.setBounds(voltageX, voltageY + 195, 70, labelHeight);
    
    randomRangeKnob.setBounds(voltageX, voltageY + 215, smallKnobSize, smallKnobSize + 16);
    randomRateKnob.setBounds(voltageX + 65, voltageY + 215, smallKnobSize, smallKnobSize + 16);
    randomSmoothKnob.setBounds(voltageX + 130, voltageY + 215, smallKnobSize, smallKnobSize + 16);
    randomModeBox.setBounds(voltageX + 200, voltageY + 230, 70, comboHeight);

    // === MODULATION Section ===
    int modX = 465;
    int modY = 110;
    
    modulationSectionLabel.setBounds(modX, modY, 100, labelHeight);
    
    panicKnob.setBounds(modX, modY + 25, knobSize, knobSize + 16);
    chaosKnob.setBounds(modX + 70, modY + 25, knobSize, knobSize + 16);
    speedKnob.setBounds(modX + 140, modY + 25, knobSize, knobSize + 16);

    // === FLOW Section ===
    int flowX = 575;
    int flowY = 240;
    
    flowSectionLabel.setBounds(flowX, flowY, 60, labelHeight);
    
    flowModeBox.setBounds(flowX, flowY + 25, 80, comboHeight);
    pulseRateKnob.setBounds(flowX + 90, flowY + 15, smallKnobSize, smallKnobSize + 16);
    pulseProbabilityKnob.setBounds(flowX + 155, flowY + 15, smallKnobSize, smallKnobSize + 16);
    footswitch.setBounds(flowX + 230, flowY + 10, 100, 80);

    // === TONE SHAPING Section ===
    int toneX = 25;
    int toneY = 400;
    
    toneSectionLabel.setBounds(toneX, toneY, 120, labelHeight);
    
    lowCutKnob.setBounds(toneX, toneY + 25, knobSize, knobSize + 16);
    highCutKnob.setBounds(toneX + 80, toneY + 25, knobSize, knobSize + 16);
    saturationKnob.setBounds(toneX + 160, toneY + 25, knobSize, knobSize + 16);

    // CHORUS subsection
    chorusSectionLabel.setBounds(toneX + 260, toneY, 70, labelHeight);
    
    chorusRateKnob.setBounds(toneX + 250, toneY + 25, smallKnobSize, smallKnobSize + 16);
    chorusDepthKnob.setBounds(toneX + 315, toneY + 25, smallKnobSize, smallKnobSize + 16);
    chorusMixKnob.setBounds(toneX + 380, toneY + 25, smallKnobSize, smallKnobSize + 16);

    // === OUTPUT Section ===
    int outX = 575;
    int outY = 400;
    
    outputSectionLabel.setBounds(outX, outY, 80, labelHeight);
    
    mixKnob.setBounds(outX, outY + 25, knobSize, knobSize + 16);
    outputGainKnob.setBounds(outX + 80, outY + 25, knobSize, knobSize + 16);
}
