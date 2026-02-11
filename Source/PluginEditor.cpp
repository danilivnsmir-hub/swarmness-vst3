#include "PluginProcessor.h"
#include "PluginEditor.h"

SwarmnesssAudioProcessorEditor::SwarmnesssAudioProcessorEditor(SwarmnesssAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&metalLookAndFeel);

    // Load background image 
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::background_clean_png, BinaryData::background_clean_pngSize);
    
    // Load header logo image
    headerLogoImage = juce::ImageCache::getFromMemory(BinaryData::header_logo_png, BinaryData::header_logo_pngSize);

    // Preset Panel (hidden by default)
    presetPanel = std::make_unique<PresetPanel>(audioProcessor.getPresetManager());
    
    // === v1.2.7: Preset Navigation Buttons ===
    addAndMakeVisible(prevPresetButton);
    prevPresetButton.setColour(juce::TextButton::buttonColourId, MetalLookAndFeel::getAccentOrange().darker(0.3f));
    prevPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    prevPresetButton.onClick = [this]() {
        audioProcessor.getPresetManager().loadPreviousPreset();
        refreshPresetList();
        updateDeleteButtonVisibility();
    };
    
    addAndMakeVisible(nextPresetButton);
    nextPresetButton.setColour(juce::TextButton::buttonColourId, MetalLookAndFeel::getAccentOrange().darker(0.3f));
    nextPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    nextPresetButton.onClick = [this]() {
        audioProcessor.getPresetManager().loadNextPreset();
        refreshPresetList();
        updateDeleteButtonVisibility();
    };
    
    // === Preset Dropdown ===
    addAndMakeVisible(presetSelector);
    refreshPresetList();
    presetSelector.onChange = [this]() {
        auto selectedName = presetSelector.getText();
        // v1.2.7: Handle * prefix instead of suffix
        if (selectedName.startsWith("* "))
            selectedName = selectedName.substring(2);
        audioProcessor.getPresetManager().loadPreset(selectedName);
        updateDeleteButtonVisibility();
    };
    
    // === SAVE Button ===
    addAndMakeVisible(savePresetButton);
    savePresetButton.setColour(juce::TextButton::buttonColourId, MetalLookAndFeel::getAccentOrange().darker(0.2f));
    savePresetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    savePresetButton.onClick = [this]() {
        auto currentName = audioProcessor.getPresetManager().getCurrentPresetName();
        if (audioProcessor.getPresetManager().isFactoryPreset(currentName)) {
            // Can't overwrite factory preset - show Save As dialog
            saveAsButton.triggerClick();
        } else {
            audioProcessor.getPresetManager().saveCurrentPreset();
            refreshPresetList();
        }
    };
    
    // === SAVE AS Button ===
    addAndMakeVisible(saveAsButton);
    saveAsButton.setColour(juce::TextButton::buttonColourId, MetalLookAndFeel::getAccentOrange().darker(0.2f));
    saveAsButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    saveAsButton.onClick = [this]() {
        auto currentName = audioProcessor.getPresetManager().getCurrentPresetName();
        if (currentName.isEmpty()) currentName = "New Preset";
        
        auto* window = new juce::AlertWindow("Save Preset As", "Enter preset name:",
                                              juce::AlertWindow::NoIcon);
        window->addTextEditor("name", currentName);
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        
        window->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, window](int result) {
                if (result == 1) {
                    auto name = window->getTextEditorContents("name");
                    if (name.isNotEmpty()) {
                        audioProcessor.getPresetManager().savePresetAs(name);
                        refreshPresetList();
                        updateDeleteButtonVisibility();
                    }
                }
                delete window;
            }), true);
    };
    
    // === DELETE Button (orange theme) ===
    addAndMakeVisible(deletePresetButton);
    deletePresetButton.setColour(juce::TextButton::buttonColourId, MetalLookAndFeel::getAccentOrange());
    deletePresetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deletePresetButton.onClick = [this]() {
        auto presetName = audioProcessor.getPresetManager().getCurrentPresetName();
        
        if (audioProcessor.getPresetManager().isFactoryPreset(presetName)) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Cannot Delete",
                "Factory presets cannot be deleted.",
                "OK");
            return;
        }
        
        if (presetName.isNotEmpty()) {
            juce::AlertWindow::showOkCancelBox(
                juce::MessageBoxIconType::QuestionIcon,
                "Delete Preset?",
                "Are you sure you want to delete \"" + presetName + "\"?",
                "Delete", "Cancel",
                this,
                juce::ModalCallbackFunction::create([this, presetName](int result) {
                    if (result == 1) {
                        audioProcessor.getPresetManager().deletePreset(presetName);
                        refreshPresetList();
                        updateDeleteButtonVisibility();
                    }
                })
            );
        }
    };
    updateDeleteButtonVisibility();
    
    // === Info Button (top-right) ===
    addAndMakeVisible(infoButton);
    infoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    infoButton.setColour(juce::TextButton::textColourOffId, MetalLookAndFeel::getAccentOrange());
    infoButton.onClick = [this]() {
        infoPanel.setVisible(true);
        infoPanel.toFront(true);
    };
    
    // Info Panel (overlay)
    addChildComponent(infoPanel);

    // === LEFT PANEL: TONE (vertical faders) ===
    // v1.2.6: Low Cut & High Cut now display in Hz
    setupVerticalFader(lowCutFader, lowCutLabel, lowCutValueLabel, "LOW CUT", FaderScaleMode::LowCutHz);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowCut", lowCutFader);
    
    setupVerticalFader(highCutFader, highCutLabel, highCutValueLabel, "HIGH CUT", FaderScaleMode::HighCutHz);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highCut", highCutFader);
    
    // Mid Boost: Scale 1-10 with step 0.5
    setupVerticalFader(midBoostFader, midBoostLabel, midBoostValueLabel, "MID BOOST", FaderScaleMode::Scale1to10_Step05);
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", midBoostFader);

    // === RIGHT PANEL: OUTPUT (vertical faders) ===
    // Mix & Volume: Scale 1-10 with step 0.1
    setupVerticalFader(mixFader, mixLabel, mixValueLabel, "MIX", FaderScaleMode::Scale1to10_Step01);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixFader);
    
    // Drive: Scale 1-10 with step 0.5
    setupVerticalFader(driveFader, driveLabel, driveValueLabel, "DRIVE", FaderScaleMode::Scale1to10_Step05);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "drive", driveFader);
    
    // Volume: Scale 1-10 with step 0.1
    setupVerticalFader(volumeFader, volumeLabel, volumeValueLabel, "VOLUME", FaderScaleMode::Scale1to10_Step01);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", volumeFader);

    // === CENTER TOP: VOLTAGE Section ===
    
    // Power button for VOLTAGE (uses "engage" parameter)
    addAndMakeVisible(pitchBypassButton);
    pitchBypassButton.setClickingTogglesState(true);
    engageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "engage", pitchBypassButton);
    pitchBypassButton.onClick = [this]() { updateSectionEnableStates(); };

    // PITCH subheader label
    pitchSubLabel.setText("PITCH", juce::dontSendNotification);
    pitchSubLabel.setJustificationType(juce::Justification::centred);
    pitchSubLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    pitchSubLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(pitchSubLabel);
    
    // MODULATION subheader label
    modulationSubLabel.setText("MODULATION", juce::dontSendNotification);
    modulationSubLabel.setJustificationType(juce::Justification::centred);
    modulationSubLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    modulationSubLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(modulationSubLabel);

    // Octave dropdown
    addAndMakeVisible(octaveModeBox);
    octaveModeBox.addItem("-2 OCT", 1);
    octaveModeBox.addItem("-1 OCT", 2);
    octaveModeBox.addItem("0", 3);
    octaveModeBox.addItem("+1 OCT", 4);
    octaveModeBox.addItem("+2 OCT", 5);
    octaveModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "octaveMode", octaveModeBox);

    // PITCH knobs
    addAndMakeVisible(pitchRangeKnob);
    pitchRangeKnob.setValueSuffix(" st");
    pitchRangeKnob.setValueMultiplier(1.0f);
    pitchRangeKnob.setScaleMode(RotaryKnob::ScaleMode::Custom);  // Keep semitones as-is
    randomRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRange", pitchRangeKnob.getSlider());

    addAndMakeVisible(pitchSpeedKnob);
    pitchSpeedKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step05);  // Speed: 0.5 step
    randomRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRate", pitchSpeedKnob.getSlider());

    // v1.2.4: RISE Horizontal Slider
    riseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    riseSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    riseSlider.setPopupDisplayEnabled(false, false, this);
    riseSlider.onValueChange = [this]() {
        float value = static_cast<float>(riseSlider.getValue()) * 2000.0f;
        riseValueLabel.setText(juce::String(static_cast<int>(value)) + "ms", juce::dontSendNotification);
    };
    addAndMakeVisible(riseSlider);
    riseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "rise", riseSlider);
    
    riseLabel.setText("RISE", juce::dontSendNotification);
    riseLabel.setJustificationType(juce::Justification::centredRight);
    riseLabel.setFont(juce::Font(9.0f));
    riseLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextDim());
    addAndMakeVisible(riseLabel);
    
    riseValueLabel.setJustificationType(juce::Justification::centredLeft);
    riseValueLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    riseValueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(riseValueLabel);

    // MODULATION knobs
    addAndMakeVisible(angerKnob);
    angerKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step01);  // Anger: 0.1 step
    chaosAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chaos", angerKnob.getSlider());

    addAndMakeVisible(rushKnob);
    rushKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step01);  // Rush: 0.1 step
    panicAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "panic", rushKnob.getSlider());

    addAndMakeVisible(modRateKnob);
    modRateKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step05);  // Rate: 0.5 step
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "speed", modRateKnob.getSlider());

    // === CENTER BOTTOM LEFT: SWARM Section ===
    addAndMakeVisible(swarmBypassButton);
    swarmBypassButton.setClickingTogglesState(true);
    chorusEngageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "chorusEngage", swarmBypassButton);
    swarmBypassButton.onClick = [this]() { updateSectionEnableStates(); };

    addAndMakeVisible(swarmDepthKnob);
    swarmDepthKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step01);  // Depth: 0.1 step
    chorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusDepth", swarmDepthKnob.getSlider());

    addAndMakeVisible(swarmRateKnob);
    swarmRateKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step01);  // Chorus Rate: 0.1 step
    chorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusRate", swarmRateKnob.getSlider());

    addAndMakeVisible(swarmMixKnob);
    swarmMixKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step01);  // Mix: 0.1 step
    chorusMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusMix", swarmMixKnob.getSlider());

    addAndMakeVisible(deepModeButton);
    deepModeButton.setColour(juce::ToggleButton::textColourId, MetalLookAndFeel::getTextLight());
    deepModeButton.setColour(juce::ToggleButton::tickColourId, MetalLookAndFeel::getAccentOrange());
    chorusModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "chorusMode", deepModeButton);
    
    deepModeLabel.setText("DEEP", juce::dontSendNotification);
    deepModeLabel.setJustificationType(juce::Justification::centred);
    deepModeLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    deepModeLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(deepModeLabel);

    // === CENTER BOTTOM RIGHT: FLOW Section ===
    addAndMakeVisible(flowBypassButton);
    flowBypassButton.setClickingTogglesState(true);
    flowEngageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "flowEngage", flowBypassButton);
    flowBypassButton.onClick = [this]() { updateSectionEnableStates(); };

    addAndMakeVisible(flowAmountKnob);
    flowAmountKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step05);  // Flow Amount: 0.5 step
    flowAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "flowAmount", flowAmountKnob.getSlider());

    addAndMakeVisible(flowSpeedKnob);
    flowSpeedKnob.setScaleMode(RotaryKnob::ScaleMode::Scale1to10_Step05);  // Flow Speed: 0.5 step
    flowSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "flowSpeed", flowSpeedKnob.getSlider());

    // === BYPASS Footswitch ===
    addAndMakeVisible(bypassFootswitch);
    bypassFootswitch.onClick = [this](bool isOn) {
        auto* param = audioProcessor.getAPVTS().getParameter("globalBypass");
        if (param) {
            param->setValueNotifyingHost(isOn ? 0.0f : 1.0f);
        }
    };

    // Start timer for LED updates and section enable states
    startTimerHz(30);

    // v1.2.4: Set plugin window size (optimized for 8px grid)
    setSize(800, 560);
    
    // Initial update of section enable states
    updateSectionEnableStates();
}

SwarmnesssAudioProcessorEditor::~SwarmnesssAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SwarmnesssAudioProcessorEditor::setupVerticalFader(juce::Slider& fader, juce::Label& label, 
                                                         juce::Label& valueLabel, const juce::String& labelText,
                                                         FaderScaleMode scaleMode) {
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader.setPopupDisplayEnabled(false, false, this);
    fader.onValueChange = [&fader, &valueLabel, scaleMode]() {
        float normalizedValue = static_cast<float>(fader.getValue());
        juce::String valueText;
        
        switch (scaleMode) {
            case FaderScaleMode::Scale1to10_Step01: {
                float displayValue = 1.0f + normalizedValue * 9.0f;
                valueText = juce::String(displayValue, 1);
                break;
            }
            case FaderScaleMode::Scale1to10_Step05: {
                float displayValue = 1.0f + normalizedValue * 9.0f;
                float rounded = std::round(displayValue * 2.0f) / 2.0f;
                valueText = juce::String(rounded, 1);
                break;
            }
            case FaderScaleMode::LowCutHz: {
                // Low Cut: 20-2000 Hz (logarithmic scale)
                float hz = 20.0f * std::pow(100.0f, normalizedValue);
                if (hz >= 1000.0f) {
                    valueText = juce::String(hz / 1000.0f, 1) + "k";
                } else {
                    valueText = juce::String(static_cast<int>(hz)) + " Hz";
                }
                break;
            }
            case FaderScaleMode::HighCutHz: {
                // High Cut: 1000-20000 Hz (logarithmic scale)
                float hz = 1000.0f * std::pow(20.0f, normalizedValue);
                if (hz >= 1000.0f) {
                    valueText = juce::String(hz / 1000.0f, 1) + "k";
                } else {
                    valueText = juce::String(static_cast<int>(hz)) + " Hz";
                }
                break;
            }
            case FaderScaleMode::Custom:
            case FaderScaleMode::Percent:
            default: {
                int value = static_cast<int>(normalizedValue * 100.0f);
                valueText = juce::String(value) + "%";
                break;
            }
        }
        valueLabel.setText(valueText, juce::dontSendNotification);
    };
    addAndMakeVisible(fader);
    
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(9.0f));
    label.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextDim());
    addAndMakeVisible(label);
    
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    valueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(valueLabel);
}

void SwarmnesssAudioProcessorEditor::timerCallback() {
    // Update bypass footswitch LED
    auto bypassed = *audioProcessor.getAPVTS().getRawParameterValue("globalBypass") > 0.5f;
    bypassFootswitch.setLEDState(bypassed ? FootswitchButton::DimRed : FootswitchButton::BrightRed);
    bypassFootswitch.setOn(!bypassed);
    
    // Periodically update section enable states
    updateSectionEnableStates();
    
    // Update preset name with dirty indicator
    updatePresetName();
}

void SwarmnesssAudioProcessorEditor::setSectionEnabled(std::vector<juce::Component*> components, bool enabled) {
    float alpha = enabled ? 1.0f : 0.35f;
    for (auto* comp : components) {
        comp->setAlpha(alpha);
        comp->setEnabled(enabled);
    }
}

void SwarmnesssAudioProcessorEditor::updateSectionEnableStates() {
    // VOLTAGE section
    bool voltageEnabled = pitchBypassButton.getToggleState();
    setSectionEnabled({
        &octaveModeBox, &pitchRangeKnob, &pitchSpeedKnob, &riseSlider,
        &riseLabel, &riseValueLabel, &pitchSubLabel,
        &angerKnob, &rushKnob, &modRateKnob, &modulationSubLabel
    }, voltageEnabled);
    
    // SWARM section
    bool swarmEnabled = swarmBypassButton.getToggleState();
    setSectionEnabled({
        &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob, &deepModeButton, &deepModeLabel
    }, swarmEnabled);
    
    // FLOW section
    bool flowEnabled = flowBypassButton.getToggleState();
    setSectionEnabled({
        &flowAmountKnob, &flowSpeedKnob
    }, flowEnabled);
}

void SwarmnesssAudioProcessorEditor::paint(juce::Graphics& g) {
    // v1.2.4: Use grid constants
    const int contentY = HEADER_HEIGHT + PADDING;
    const int contentHeight = getHeight() - HEADER_HEIGHT - PADDING * 2;
    const int centerX = SIDE_PANEL_WIDTH + PADDING;
    const int centerWidth = getWidth() - SIDE_PANEL_WIDTH * 2 - PADDING * 2;
    
    // v1.2.4: Dark background
    g.fillAll(juce::Colour(0xff1A1A1A));
    
    // Draw background image in content area (if valid)
    if (backgroundImage.isValid()) {
        auto contentArea = juce::Rectangle<float>((float)SIDE_PANEL_WIDTH, (float)HEADER_HEIGHT, 
                                                   (float)(getWidth() - SIDE_PANEL_WIDTH * 2), 
                                                   (float)(getHeight() - HEADER_HEIGHT));
        g.setOpacity(0.25f);
        g.drawImage(backgroundImage, contentArea, juce::RectanglePlacement::stretchToFit);
        g.setOpacity(1.0f);
    }
    
    // === HEADER PANEL v1.2.4 ===
    g.setColour(juce::Colour(0xff1A1A1A));
    g.fillRect(0, 0, getWidth(), HEADER_HEIGHT);
    
    // Orange bottom line
    g.setColour(MetalLookAndFeel::getAccentOrange());
    g.fillRect(0, HEADER_HEIGHT - 1, getWidth(), 1);
    
    // Draw header logo (aligned to right edge of VOLTAGE section)
    if (headerLogoImage.isValid()) {
        float logoH = 36.0f;
        float logoScale = logoH / (float)headerLogoImage.getHeight();
        float logoW = headerLogoImage.getWidth() * logoScale;
        // v1.2.8: Align with right edge of VOLTAGE section (before right side panel)
        float voltageRightEdge = (float)(getWidth() - SIDE_PANEL_WIDTH - PADDING);
        float logoX = voltageRightEdge - logoW;
        float logoY = (HEADER_HEIGHT - logoH) * 0.5f;
        
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(headerLogoImage, 
                    juce::Rectangle<float>(logoX, logoY, logoW, logoH),
                    juce::RectanglePlacement::centred);
    }

    // === SIDE PANELS ===
    g.setColour(juce::Colour(0xff1A1A1A));
    g.fillRect(0, HEADER_HEIGHT, SIDE_PANEL_WIDTH, getHeight() - HEADER_HEIGHT);
    g.fillRect(getWidth() - SIDE_PANEL_WIDTH, HEADER_HEIGHT, SIDE_PANEL_WIDTH, getHeight() - HEADER_HEIGHT);
    
    // === DIVIDER LINES v1.2.4 ===
    g.setColour(juce::Colour(0xff333333));
    
    // Vertical dividers at side panels
    g.drawLine((float)SIDE_PANEL_WIDTH, (float)HEADER_HEIGHT, (float)SIDE_PANEL_WIDTH, (float)getHeight(), 1.0f);
    g.drawLine((float)(getWidth() - SIDE_PANEL_WIDTH), (float)HEADER_HEIGHT, 
               (float)(getWidth() - SIDE_PANEL_WIDTH), (float)getHeight(), 1.0f);
    
    // Horizontal divider between VOLTAGE and SWARM/FLOW
    int dividerY = HEADER_HEIGHT + 200;
    g.drawLine((float)SIDE_PANEL_WIDTH, (float)dividerY, (float)(getWidth() - SIDE_PANEL_WIDTH), (float)dividerY, 1.0f);
    
    // Vertical divider between left and right columns
    int centerDividerX = getWidth() / 2;
    g.drawLine((float)centerDividerX, (float)HEADER_HEIGHT, (float)centerDividerX, (float)(getHeight() - 80), 1.0f);
    
    // === VOLTAGE SECTION FRAME v1.2.4 ===
    int voltageLeft = SIDE_PANEL_WIDTH + PADDING;
    int voltageTop = HEADER_HEIGHT + PADDING;
    int voltageWidth = getWidth() - SIDE_PANEL_WIDTH * 2 - PADDING * 2;
    int voltageHeight = dividerY - voltageTop - PADDING;
    
    // Section frame
    g.setColour(juce::Colour(0xff222222));
    g.fillRoundedRectangle((float)voltageLeft, (float)voltageTop, (float)voltageWidth, (float)voltageHeight, 8.0f);
    g.setColour(juce::Colour(0xff404040));
    g.drawRoundedRectangle((float)voltageLeft, (float)voltageTop, (float)voltageWidth, (float)voltageHeight, 8.0f, 1.0f);
    
    // === SECTION TITLES v1.2.4 ===
    g.setColour(MetalLookAndFeel::getAccentOrange());
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    
    // VOLTAGE title (centered above both columns)
    g.drawText("VOLTAGE", voltageLeft, voltageTop + 6, voltageWidth - 40, 18, juce::Justification::centred);
    
    // SWARM title (left column)
    int columnWidth = (centerWidth - GAP) / 2;
    g.drawText("SWARM", SIDE_PANEL_WIDTH + PADDING, dividerY + 8, columnWidth - 30, 18, juce::Justification::centred);
    
    // FLOW title (right column)
    g.drawText("FLOW", centerDividerX + PADDING, dividerY + 8, columnWidth - 30, 18, juce::Justification::centred);
    
    // v1.2.4: Version number (bottom right corner, 8px padding, 40% opacity)
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::Font(10.0f));
    g.drawText("v1.2.8", getWidth() - 50 - GRID, getHeight() - 16 - GRID, 50, 16, juce::Justification::right);
}

void SwarmnesssAudioProcessorEditor::resized() {
    // v1.2.4: Use grid constants
    const int contentY = HEADER_HEIGHT + PADDING;
    const int contentHeight = getHeight() - HEADER_HEIGHT - PADDING * 2;
    const int sidePanelWidth = SIDE_PANEL_WIDTH;
    const int centerX = sidePanelWidth + PADDING;
    const int centerWidth = getWidth() - sidePanelWidth * 2 - PADDING * 2;
    const int columnWidth = (centerWidth - GAP) / 2;
    const int leftColumnX = centerX;
    const int rightColumnX = centerX + columnWidth + GAP;
    const int dividerY = HEADER_HEIGHT + 200;
    const int powerButtonSize = 24;
    const int faderWidth = 32;
    const int faderHeight = 80;
    
    // === HEADER: v1.2.7 New Preset UI Layout ===
    // [<] [>] [Preset Name ▼ (220px)] SAVE SAVE_AS DELETE ... [Logo centered] ... [i]
    int buttonHeight = 28;
    int buttonY = PADDING + (HEADER_HEIGHT - PADDING * 2 - buttonHeight) / 2;
    int arrowSize = 32;
    
    prevPresetButton.setBounds(PADDING, buttonY, arrowSize, buttonHeight);
    nextPresetButton.setBounds(prevPresetButton.getRight() + 4, buttonY, arrowSize, buttonHeight);
    
    // Wider dropdown for preset names
    int dropdownWidth = 180;
    presetSelector.setBounds(nextPresetButton.getRight() + 8, buttonY, dropdownWidth, buttonHeight);
    
    // SAVE, SAVE AS, DELETE buttons
    int btnWidth = 50;
    savePresetButton.setBounds(presetSelector.getRight() + 8, buttonY, btnWidth, buttonHeight);
    saveAsButton.setBounds(savePresetButton.getRight() + 4, buttonY, btnWidth + 20, buttonHeight);
    deletePresetButton.setBounds(saveAsButton.getRight() + 4, buttonY, btnWidth + 10, buttonHeight);
    
    // === Info Button (top-right in header) ===
    infoButton.setBounds(getWidth() - PADDING - 32, PADDING, 32, 32);
    
    // Info Panel (full screen overlay)
    infoPanel.setBounds(getLocalBounds());
    
    // === LEFT PANEL: TONE (3 vertical faders, centered with 16px gaps) ===
    {
        int sideSliderHeight = 80;
        int sideSliderWidth = 32;
        int sideGap = 16;  // v1.2.6: increased from 8px to 16px
        int totalSlidersHeight = sideSliderHeight * 3 + sideGap * 2; // 3 faders + 2 gaps
        int sideStartY = HEADER_HEIGHT + (getHeight() - HEADER_HEIGHT - totalSlidersHeight) / 2;
        int sliderX = PADDING + (sidePanelWidth - sideSliderWidth) / 2;
        
        // LOW CUT fader
        lowCutFader.setBounds(sliderX, sideStartY, sideSliderWidth, sideSliderHeight);
        lowCutLabel.setBounds(PADDING, sideStartY + sideSliderHeight + 2, sidePanelWidth, 12);
        lowCutValueLabel.setBounds(PADDING, sideStartY + sideSliderHeight + 14, sidePanelWidth, 12);
        
        // HIGH CUT fader
        int highCutY = sideStartY + sideSliderHeight + sideGap;
        highCutFader.setBounds(sliderX, highCutY, sideSliderWidth, sideSliderHeight);
        highCutLabel.setBounds(PADDING, highCutY + sideSliderHeight + 2, sidePanelWidth, 12);
        highCutValueLabel.setBounds(PADDING, highCutY + sideSliderHeight + 14, sidePanelWidth, 12);
        
        // MID BOOST fader
        int midBoostY = highCutY + sideSliderHeight + sideGap;
        midBoostFader.setBounds(sliderX, midBoostY, sideSliderWidth, sideSliderHeight);
        midBoostLabel.setBounds(PADDING - 4, midBoostY + sideSliderHeight + 2, sidePanelWidth + 8, 12);
        midBoostValueLabel.setBounds(PADDING, midBoostY + sideSliderHeight + 14, sidePanelWidth, 12);
    }
    
    // === RIGHT PANEL: OUTPUT (3 vertical faders, centered with 16px gaps) ===
    {
        int rightSideX = getWidth() - PADDING - sidePanelWidth;
        int sideSliderHeight = 80;
        int sideSliderWidth = 32;
        int sideGap = 16;  // v1.2.6: increased from 8px to 16px
        int totalSlidersHeight = sideSliderHeight * 3 + sideGap * 2; // 3 faders + 2 gaps
        int sideStartY = HEADER_HEIGHT + (getHeight() - HEADER_HEIGHT - totalSlidersHeight) / 2;
        int sliderX = rightSideX + (sidePanelWidth - sideSliderWidth) / 2;
        
        // MIX fader
        mixFader.setBounds(sliderX, sideStartY, sideSliderWidth, sideSliderHeight);
        mixLabel.setBounds(rightSideX, sideStartY + sideSliderHeight + 2, sidePanelWidth, 12);
        mixValueLabel.setBounds(rightSideX, sideStartY + sideSliderHeight + 14, sidePanelWidth, 12);
        
        // DRIVE fader
        int driveY = sideStartY + sideSliderHeight + sideGap;
        driveFader.setBounds(sliderX, driveY, sideSliderWidth, sideSliderHeight);
        driveLabel.setBounds(rightSideX, driveY + sideSliderHeight + 2, sidePanelWidth, 12);
        driveValueLabel.setBounds(rightSideX, driveY + sideSliderHeight + 14, sidePanelWidth, 12);
        
        // VOLUME fader
        int volumeY = driveY + sideSliderHeight + sideGap;
        volumeFader.setBounds(sliderX, volumeY, sideSliderWidth, sideSliderHeight);
        volumeLabel.setBounds(rightSideX, volumeY + sideSliderHeight + 2, sidePanelWidth, 12);
        volumeValueLabel.setBounds(rightSideX, volumeY + sideSliderHeight + 14, sidePanelWidth, 12);
    }
    
    // === CENTER TOP: VOLTAGE Section v1.2.4 ===
    {
        int voltageTop = HEADER_HEIGHT + PADDING;
        int voltageWidth = centerWidth;
        int halfWidth = voltageWidth / 2;
        
        // Power button (top-right of VOLTAGE frame)
        pitchBypassButton.setBounds(centerX + voltageWidth - 32, voltageTop + 8, powerButtonSize, powerButtonSize);
        
        // === LEFT HALF: PITCH ===
        int pitchX = leftColumnX + 8;
        int pitchY = voltageTop + 28;
        
        // PITCH subheader
        pitchSubLabel.setBounds(pitchX, pitchY, halfWidth - 30, 14);
        
        // Row 1: Octave dropdown
        octaveModeBox.setBounds(pitchX, pitchY + 16, 88, 28);
        
        // Row 2: RANGE + SPEED knobs + RISE horizontal slider
        int knobY = pitchY + 50;
        int knobFullHeight = KNOB_SIZE + 22;
        
        pitchRangeKnob.setBounds(pitchX, knobY, KNOB_SIZE, knobFullHeight);
        pitchSpeedKnob.setBounds(pitchX + KNOB_SIZE + 8, knobY, KNOB_SIZE, knobFullHeight);
        
        // RISE horizontal slider - takes remaining width
        int riseX = pitchX + (KNOB_SIZE + 8) * 2;
        int riseWidth = halfWidth - (KNOB_SIZE + 8) * 2 - GAP;
        int riseY = knobY + (KNOB_SIZE - SLIDER_HEIGHT) / 2;  // Centered with knobs
        
        riseSlider.setBounds(riseX, riseY, riseWidth - 45, SLIDER_HEIGHT);
        riseLabel.setBounds(riseX - 2, riseY - 12, 40, 12);
        riseValueLabel.setBounds(riseSlider.getRight() + 4, riseY + 4, 45, 14);
        
        // === RIGHT HALF: MODULATION ===
        int modX = rightColumnX - GAP / 2;
        int modY = voltageTop + 28;
        
        // MODULATION subheader
        modulationSubLabel.setBounds(modX, modY, halfWidth - 20, 14);
        
        // ANGER, RUSH, RATE knobs (vertical arrangement to match PITCH height)
        int modKnobY = modY + 20;
        int modKnobSpacing = (knobY + knobFullHeight - modKnobY) / 3;
        
        // Centered horizontally in the right column
        int modKnobX = modX + (halfWidth - KNOB_SIZE) / 2 - 10;
        
        angerKnob.setBounds(modKnobX - 60, modKnobY, KNOB_SIZE, knobFullHeight);
        rushKnob.setBounds(modKnobX, modKnobY, KNOB_SIZE, knobFullHeight);
        modRateKnob.setBounds(modKnobX + 60, modKnobY, KNOB_SIZE, knobFullHeight);
    }
    
    // === CENTER BOTTOM: SWARM + FLOW ===
    int swarmFlowY = dividerY + GAP;
    int swarmFlowHeight = getHeight() - dividerY - 100;
    
    // SWARM Section (left column)
    {
        int baseX = leftColumnX;
        int knobY = swarmFlowY + 16;
        int knobFullHeight = KNOB_SIZE + 22;
        
        // Power button (top-right)
        swarmBypassButton.setBounds(baseX + columnWidth - 38, dividerY + 6, powerButtonSize, powerButtonSize);
        
        // 3 knobs horizontally
        swarmDepthKnob.setBounds(baseX + 8, knobY, KNOB_SIZE, knobFullHeight);
        swarmRateKnob.setBounds(baseX + 8 + KNOB_SIZE + 8, knobY, KNOB_SIZE, knobFullHeight);
        swarmMixKnob.setBounds(baseX + 8 + (KNOB_SIZE + 8) * 2, knobY, KNOB_SIZE, knobFullHeight);
        
        // DEEP toggle (inline)
        int toggleX = baseX + 8 + (KNOB_SIZE + 8) * 3;
        deepModeButton.setBounds(toggleX, knobY + 14, 40, 24);
        deepModeLabel.setBounds(toggleX - 2, knobY + 40, 45, 14);
    }
    
    // FLOW Section (right column)
    {
        int baseX = rightColumnX - GAP / 2 + 10;
        int knobY = swarmFlowY + 16;
        int knobFullHeight = KNOB_SIZE + 22;
        
        // Power button (top-right)
        flowBypassButton.setBounds(getWidth() - SIDE_PANEL_WIDTH - PADDING - 38, dividerY + 6, powerButtonSize, powerButtonSize);
        
        // 2 knobs
        flowAmountKnob.setBounds(baseX + 20, knobY, KNOB_SIZE, knobFullHeight);
        flowSpeedKnob.setBounds(baseX + 20 + KNOB_SIZE + GAP, knobY, KNOB_SIZE, knobFullHeight);
    }
    
    // === BYPASS Footswitch (bottom center) ===
    {
        int footWidth = 60;
        int footHeight = 60;
        int footX = (getWidth() - footWidth) / 2;
        int footY = getHeight() - footHeight - 15;
        bypassFootswitch.setBounds(footX, footY, footWidth, footHeight);
    }
}

void SwarmnesssAudioProcessorEditor::refreshPresetList() {
    presetSelector.clear();
    auto presets = audioProcessor.getPresetManager().getPresetList();
    int index = 1;
    for (const auto& name : presets) {
        presetSelector.addItem(name, index++);
    }
    
    updatePresetName();
}

void SwarmnesssAudioProcessorEditor::updatePresetName() {
    // v1.2.7: Use getDisplayName() which adds * prefix if dirty
    auto displayName = audioProcessor.getPresetManager().getDisplayName();
    if (displayName.isNotEmpty()) {
        presetSelector.setText(displayName, juce::dontSendNotification);
    }
}

void SwarmnesssAudioProcessorEditor::updateDeleteButtonVisibility() {
    auto presetName = audioProcessor.getPresetManager().getCurrentPresetName();
    bool isUserPreset = !audioProcessor.getPresetManager().isFactoryPreset(presetName) && presetName.isNotEmpty();
    deletePresetButton.setVisible(isUserPreset);
    deletePresetButton.setEnabled(isUserPreset);
}
