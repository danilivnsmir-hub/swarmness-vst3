#include "PluginProcessor.h"
#include "PluginEditor.h"

SwarmnesssAudioProcessorEditor::SwarmnesssAudioProcessorEditor(SwarmnesssAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&metalLookAndFeel);

    // Preset Panel
    presetPanel = std::make_unique<PresetPanel>(audioProcessor.getPresetManager());
    addAndMakeVisible(*presetPanel);

    // === Setup Labels ===
    setupLabel(pitchSectionLabel, "PITCH");           // was VOLTAGE
    setupLabel(slideSectionLabel, "SLIDE", false);
    setupLabel(randomSectionLabel, "RANDOM", false);
    setupLabel(modulationSectionLabel, "MODULATION");
    setupLabel(toneSectionLabel, "TONE SHAPING");
    setupLabel(chorusSectionLabel, "CHORUS", false);
    setupLabel(outputSectionLabel, "OUTPUT");
    setupLabel(pulseSectionLabel, "PULSE");           // was FLOW

    // === PITCH Section (was VOLTAGE) ===
    addAndMakeVisible(octaveModeBox);
    octaveModeBox.addItem("-2 OCT", 1);
    octaveModeBox.addItem("-1 OCT", 2);
    octaveModeBox.addItem("+1 OCT", 3);
    octaveModeBox.addItem("+2 OCT", 4);
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

    // === PULSE Section (was FLOW) ===
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

    // Pulse Footswitch (left)
    addAndMakeVisible(pulseFootswitch);
    pulseFootswitch.onClick = [this](bool isOn) {
        audioProcessor.getFlowEngine().setStaticState(isOn);
    };

    // Bypass Footswitch (right)
    addAndMakeVisible(bypassFootswitch);
    bypassFootswitch.onClick = [this](bool isOn) {
        auto* param = audioProcessor.getAPVTS().getParameter("globalBypass");
        if (param) {
            param->setValueNotifyingHost(isOn ? 1.0f : 0.0f);
        }
    };

    // Start timer for LED updates
    startTimerHz(30);

    // IMPORTANT: setSize() must be called LAST after all components are initialized
    setSize(900, 600);
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
    // Update pulse footswitch LED based on flow engine state
    // Dim red when off/inactive, bright red when on/active
    auto flowMode = static_cast<int>(*audioProcessor.getAPVTS().getRawParameterValue("flowMode"));
    if (flowMode == 1) {  // Pulse mode
        pulseFootswitch.setLEDState(FootswitchButton::OrangeBlinking);
    } else {
        pulseFootswitch.setLEDState(audioProcessor.getFlowEngine().isCurrentlyOn() ? 
            FootswitchButton::BrightRed : FootswitchButton::DimRed);
    }
    
    // Update bypass footswitch LED - dim red (off/processing), bright red (on/bypassed)
    auto bypassed = *audioProcessor.getAPVTS().getRawParameterValue("globalBypass") > 0.5f;
    bypassFootswitch.setLEDState(bypassed ? FootswitchButton::BrightRed : FootswitchButton::DimRed);
    bypassFootswitch.setOn(bypassed);
}

void SwarmnesssAudioProcessorEditor::paint(juce::Graphics& g) {
    // Background
    g.fillAll(MetalLookAndFeel::getBackgroundDark());

    // Title
    g.setColour(MetalLookAndFeel::getTextLight());
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("SWARMNESS", 0, 8, getWidth(), 25, juce::Justification::centred);

    g.setColour(MetalLookAndFeel::getTextDim());
    g.setFont(juce::Font(10.0f));
    g.drawText("v2.2.0", 0, 30, getWidth(), 15, juce::Justification::centred);

    // Section backgrounds - each section on its own row
    auto drawSectionBg = [&](juce::Rectangle<int> bounds) {
        g.setColour(MetalLookAndFeel::getMetalGrey().withAlpha(0.3f));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(MetalLookAndFeel::getMetalLight().withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };

    // Row 1: PITCH section
    drawSectionBg(juce::Rectangle<int>(10, 50, 880, 130));
    
    // Row 2: MODULATION section
    drawSectionBg(juce::Rectangle<int>(10, 185, 880, 85));
    
    // Row 3: TONE SHAPING + OUTPUT section
    drawSectionBg(juce::Rectangle<int>(10, 275, 650, 95));
    drawSectionBg(juce::Rectangle<int>(670, 275, 220, 95));
    
    // Row 4: PULSE section
    drawSectionBg(juce::Rectangle<int>(10, 375, 880, 85));
    
    // Row 5: Footswitches
    drawSectionBg(juce::Rectangle<int>(10, 465, 880, 125));

    // Footer
    g.setColour(MetalLookAndFeel::getTextDim().withAlpha(0.5f));
    g.setFont(juce::Font(9.0f));
    g.drawText("© 2026 OpenAudio • Pitch Shifter for Metalcore/Djent", 
               0, getHeight() - 15, getWidth(), 15, juce::Justification::centred);
}

void SwarmnesssAudioProcessorEditor::resized() {
    const int knobSize = 60;
    const int smallKnobSize = 55;
    const int comboHeight = 22;
    const int toggleHeight = 22;
    const int labelHeight = 18;
    const int margin = 10;
    const int sectionPadding = 8;

    // Preset Panel (top)
    presetPanel->setBounds(10, 5, getWidth() - 20, 30);

    // === ROW 1: PITCH Section (y=50, height=130) ===
    int pitchY = 55;
    
    pitchSectionLabel.setBounds(20, pitchY, 60, labelHeight);
    
    // Core pitch controls
    octaveModeBox.setBounds(20, pitchY + 20, 75, comboHeight);
    engageButton.setBounds(100, pitchY + 20, 45, toggleHeight);
    riseKnob.setBounds(150, pitchY + 12, knobSize, knobSize + 14);

    // SLIDE subsection
    slideSectionLabel.setBounds(230, pitchY, 50, labelHeight);
    slideRangeKnob.setBounds(220, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    slideTimeKnob.setBounds(280, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    slidePositionKnob.setBounds(340, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    slideDirectionBox.setBounds(405, pitchY + 22, 60, comboHeight);
    autoSlideButton.setBounds(405, pitchY + 48, 50, toggleHeight);
    slideReturnButton.setBounds(458, pitchY + 48, 45, toggleHeight);

    // RANDOM subsection
    randomSectionLabel.setBounds(525, pitchY, 60, labelHeight);
    randomRangeKnob.setBounds(515, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    randomRateKnob.setBounds(575, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    randomSmoothKnob.setBounds(635, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    randomModeBox.setBounds(700, pitchY + 35, 60, comboHeight);

    // OUTPUT section (inline with PITCH row, right side)
    outputSectionLabel.setBounds(780, pitchY, 80, labelHeight);
    mixKnob.setBounds(770, pitchY + 18, smallKnobSize, smallKnobSize + 12);
    outputGainKnob.setBounds(830, pitchY + 18, smallKnobSize, smallKnobSize + 12);

    // === ROW 2: MODULATION Section (y=185, height=85) ===
    int modY = 190;
    
    modulationSectionLabel.setBounds(20, modY, 100, labelHeight);
    panicKnob.setBounds(20, modY + 18, knobSize, knobSize + 14);
    chaosKnob.setBounds(90, modY + 18, knobSize, knobSize + 14);
    speedKnob.setBounds(160, modY + 18, knobSize, knobSize + 14);

    // === ROW 3: TONE SHAPING Section (y=275, height=95) ===
    int toneY = 280;
    
    toneSectionLabel.setBounds(20, toneY, 110, labelHeight);
    lowCutKnob.setBounds(20, toneY + 18, smallKnobSize, smallKnobSize + 12);
    highCutKnob.setBounds(80, toneY + 18, smallKnobSize, smallKnobSize + 12);
    saturationKnob.setBounds(140, toneY + 18, smallKnobSize, smallKnobSize + 12);

    // CHORUS subsection
    chorusSectionLabel.setBounds(220, toneY, 60, labelHeight);
    chorusRateKnob.setBounds(210, toneY + 18, smallKnobSize, smallKnobSize + 12);
    chorusDepthKnob.setBounds(270, toneY + 18, smallKnobSize, smallKnobSize + 12);
    chorusMixKnob.setBounds(330, toneY + 18, smallKnobSize, smallKnobSize + 12);

    // === ROW 4: PULSE Section (y=375, height=85) ===
    int pulseY = 380;
    
    pulseSectionLabel.setBounds(20, pulseY, 60, labelHeight);
    flowModeBox.setBounds(20, pulseY + 22, 70, comboHeight);
    pulseRateKnob.setBounds(100, pulseY + 12, smallKnobSize, smallKnobSize + 12);
    pulseProbabilityKnob.setBounds(160, pulseY + 12, smallKnobSize, smallKnobSize + 12);

    // === ROW 5: Footswitches (y=465, height=125) ===
    int footY = 475;
    int footWidth = 100;
    int footHeight = 100;
    int footSpacing = 150;
    int footCenterX = getWidth() / 2;
    
    // PULSE footswitch (left)
    pulseFootswitch.setBounds(footCenterX - footSpacing - footWidth/2, footY, footWidth, footHeight);
    
    // BYPASS footswitch (right)
    bypassFootswitch.setBounds(footCenterX + footSpacing - footWidth/2, footY, footWidth, footHeight);
}
