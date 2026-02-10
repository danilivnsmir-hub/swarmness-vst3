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
    
    // === Preset Dropdown (top-left) ===
    addAndMakeVisible(presetSelector);
    refreshPresetList();
    presetSelector.onChange = [this]() {
        auto selectedName = presetSelector.getText();
        audioProcessor.getPresetManager().loadPreset(selectedName);
    };
    
    // === Preset Buttons ===
    addAndMakeVisible(savePresetButton);
    savePresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    savePresetButton.setColour(juce::TextButton::textColourOffId, MetalLookAndFeel::getAccentOrange());
    savePresetButton.onClick = [this]() {
        auto presetName = presetSelector.getText();
        if (presetName.endsWith(" *"))
            presetName = presetName.dropLastCharacters(2);
        if (presetName.isEmpty()) presetName = "New Preset";
        
        auto* window = new juce::AlertWindow("Save Preset", "Enter preset name:",
                                              juce::AlertWindow::QuestionIcon);
        window->addTextEditor("name", presetName);
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        
        window->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, window](int result) {
                if (result == 1) {
                    auto name = window->getTextEditorContents("name");
                    if (name.isNotEmpty()) {
                        audioProcessor.getPresetManager().savePreset(name);
                        refreshPresetList();
                    }
                }
                delete window;
            }), true);
    };
    
    addAndMakeVisible(exportPresetButton);
    exportPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    exportPresetButton.setColour(juce::TextButton::textColourOffId, MetalLookAndFeel::getTextLight());
    exportPresetButton.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export Preset",
            audioProcessor.getPresetManager().getPresetsDirectory(),
            "*.swpreset;*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode,
            [this, chooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{}) {
                    auto path = file.getFullPathName();
                    if (!path.endsWith(".swpreset") && !path.endsWith(".json"))
                        path += ".swpreset";
                    audioProcessor.getPresetManager().exportPreset(juce::File(path));
                }
            });
    };
    
    addAndMakeVisible(importPresetButton);
    importPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    importPresetButton.setColour(juce::TextButton::textColourOffId, MetalLookAndFeel::getTextLight());
    importPresetButton.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Import Preset",
            audioProcessor.getPresetManager().getPresetsDirectory(),
            "*.swpreset;*.json");
        chooser->launchAsync(juce::FileBrowserComponent::openMode,
            [this, chooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    audioProcessor.getPresetManager().importPreset(file);
                    refreshPresetList();
                }
            });
    };
    
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
    setupVerticalFader(lowCutFader, lowCutLabel, lowCutValueLabel, "LOW CUT");
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowCut", lowCutFader);
    
    setupVerticalFader(highCutFader, highCutLabel, highCutValueLabel, "HIGH CUT");
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highCut", highCutFader);
    
    setupVerticalFader(midBoostFader, midBoostLabel, midBoostValueLabel, "MID BOOST");
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", midBoostFader);

    // === RIGHT PANEL: OUTPUT (vertical faders) ===
    setupVerticalFader(mixFader, mixLabel, mixValueLabel, "MIX");
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixFader);
    
    setupVerticalFader(driveFader, driveLabel, driveValueLabel, "DRIVE");
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "drive", driveFader);
    
    setupVerticalFader(volumeFader, volumeLabel, volumeValueLabel, "VOLUME");
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
    randomRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRange", pitchRangeKnob.getSlider());

    addAndMakeVisible(pitchSpeedKnob);
    pitchSpeedKnob.setValueSuffix("%");
    pitchSpeedKnob.setValueMultiplier(100.0f);
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
    angerKnob.setValueSuffix("%");
    angerKnob.setValueMultiplier(100.0f);
    chaosAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chaos", angerKnob.getSlider());

    addAndMakeVisible(rushKnob);
    rushKnob.setValueSuffix("%");
    rushKnob.setValueMultiplier(100.0f);
    panicAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "panic", rushKnob.getSlider());

    addAndMakeVisible(modRateKnob);
    modRateKnob.setValueSuffix("%");
    modRateKnob.setValueMultiplier(100.0f);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "speed", modRateKnob.getSlider());

    // === CENTER BOTTOM LEFT: SWARM Section ===
    addAndMakeVisible(swarmBypassButton);
    swarmBypassButton.setClickingTogglesState(true);
    chorusEngageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "chorusEngage", swarmBypassButton);
    swarmBypassButton.onClick = [this]() { updateSectionEnableStates(); };

    addAndMakeVisible(swarmDepthKnob);
    swarmDepthKnob.setValueSuffix("%");
    swarmDepthKnob.setValueMultiplier(100.0f);
    chorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusDepth", swarmDepthKnob.getSlider());

    addAndMakeVisible(swarmRateKnob);
    swarmRateKnob.setValueSuffix("%");
    swarmRateKnob.setValueMultiplier(100.0f);
    chorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusRate", swarmRateKnob.getSlider());

    addAndMakeVisible(swarmMixKnob);
    swarmMixKnob.setValueSuffix("%");
    swarmMixKnob.setValueMultiplier(100.0f);
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
    flowAmountKnob.setValueSuffix("%");
    flowAmountKnob.setValueMultiplier(100.0f);
    flowAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "flowAmount", flowAmountKnob.getSlider());

    addAndMakeVisible(flowSpeedKnob);
    flowSpeedKnob.setValueSuffix("%");
    flowSpeedKnob.setValueMultiplier(100.0f);
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
                                                         float multiplier) {
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader.setPopupDisplayEnabled(false, false, this);
    fader.onValueChange = [&fader, &valueLabel, multiplier]() {
        int value = static_cast<int>(fader.getValue() * multiplier);
        valueLabel.setText(juce::String(value) + "%", juce::dontSendNotification);
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
    
    // Draw header logo (centered in entire width)
    if (headerLogoImage.isValid()) {
        float logoH = 36.0f;
        float logoScale = logoH / (float)headerLogoImage.getHeight();
        float logoW = headerLogoImage.getWidth() * logoScale;
        float logoX = (getWidth() - logoW) * 0.5f;
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
    g.drawText("v1.2.4", getWidth() - 50 - GRID, getHeight() - 16 - GRID, 50, 16, juce::Justification::right);
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
    
    // === HEADER: Preset Selector and Buttons (left) ===
    presetSelector.setBounds(PADDING, PADDING, 100, 32);
    savePresetButton.setBounds(presetSelector.getRight() + GRID, PADDING, 48, 32);
    exportPresetButton.setBounds(savePresetButton.getRight() + GRID, PADDING, 56, 32);
    importPresetButton.setBounds(exportPresetButton.getRight() + GRID, PADDING, 56, 32);
    
    // === Info Button (top-right in header) ===
    infoButton.setBounds(getWidth() - PADDING - 32, PADDING, 32, 32);
    
    // Info Panel (full screen overlay)
    infoPanel.setBounds(getLocalBounds());
    
    // === LEFT PANEL: TONE (3 vertical faders, symmetrical) ===
    {
        int sideY = contentY + 40;
        int sideSpacing = (contentHeight - 40 - faderHeight * 3) / 2;
        int sliderX = PADDING + (sidePanelWidth - faderWidth) / 2;
        
        // LOW CUT fader
        lowCutFader.setBounds(sliderX, sideY, faderWidth, faderHeight);
        lowCutLabel.setBounds(PADDING, sideY + faderHeight + 2, sidePanelWidth, 12);
        lowCutValueLabel.setBounds(PADDING, sideY + faderHeight + 14, sidePanelWidth, 12);
        
        // HIGH CUT fader
        int highCutY = sideY + faderHeight + sideSpacing;
        highCutFader.setBounds(sliderX, highCutY, faderWidth, faderHeight);
        highCutLabel.setBounds(PADDING, highCutY + faderHeight + 2, sidePanelWidth, 12);
        highCutValueLabel.setBounds(PADDING, highCutY + faderHeight + 14, sidePanelWidth, 12);
        
        // MID BOOST fader
        int midBoostY = highCutY + faderHeight + sideSpacing;
        midBoostFader.setBounds(sliderX, midBoostY, faderWidth, faderHeight);
        midBoostLabel.setBounds(PADDING - 4, midBoostY + faderHeight + 2, sidePanelWidth + 8, 12);
        midBoostValueLabel.setBounds(PADDING, midBoostY + faderHeight + 14, sidePanelWidth, 12);
    }
    
    // === RIGHT PANEL: OUTPUT (3 vertical faders, symmetrical) ===
    {
        int rightSideX = getWidth() - PADDING - sidePanelWidth;
        int sideY = contentY + 40;
        int sideSpacing = (contentHeight - 40 - faderHeight * 3) / 2;
        int sliderX = rightSideX + (sidePanelWidth - faderWidth) / 2;
        
        // MIX fader
        mixFader.setBounds(sliderX, sideY, faderWidth, faderHeight);
        mixLabel.setBounds(rightSideX, sideY + faderHeight + 2, sidePanelWidth, 12);
        mixValueLabel.setBounds(rightSideX, sideY + faderHeight + 14, sidePanelWidth, 12);
        
        // DRIVE fader
        int driveY = sideY + faderHeight + sideSpacing;
        driveFader.setBounds(sliderX, driveY, faderWidth, faderHeight);
        driveLabel.setBounds(rightSideX, driveY + faderHeight + 2, sidePanelWidth, 12);
        driveValueLabel.setBounds(rightSideX, driveY + faderHeight + 14, sidePanelWidth, 12);
        
        // VOLUME fader
        int volumeY = driveY + faderHeight + sideSpacing;
        volumeFader.setBounds(sliderX, volumeY, faderWidth, faderHeight);
        volumeLabel.setBounds(rightSideX, volumeY + faderHeight + 2, sidePanelWidth, 12);
        volumeValueLabel.setBounds(rightSideX, volumeY + faderHeight + 14, sidePanelWidth, 12);
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
    auto current = audioProcessor.getPresetManager().getCurrentPresetName();
    if (current.isNotEmpty()) {
        // Add "*" if preset is dirty (modified)
        if (audioProcessor.getPresetManager().isDirty()) {
            current += " *";
        }
        presetSelector.setText(current, juce::dontSendNotification);
    }
}
