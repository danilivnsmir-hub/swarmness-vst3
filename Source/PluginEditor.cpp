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
        // v1.2.2: Strip existing dirty indicator before suggesting name
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

    // RISE Vertical Fader
    riseFader.setSliderStyle(juce::Slider::LinearVertical);
    riseFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    riseFader.setPopupDisplayEnabled(false, false, this);
    riseFader.onValueChange = [this]() {
        float value = static_cast<float>(riseFader.getValue()) * 2000.0f;
        riseFaderValueLabel.setText(juce::String(static_cast<int>(value)) + "ms", juce::dontSendNotification);
    };
    addAndMakeVisible(riseFader);
    riseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "rise", riseFader);
    
    riseFaderLabel.setText("RISE", juce::dontSendNotification);
    riseFaderLabel.setJustificationType(juce::Justification::centred);
    riseFaderLabel.setFont(juce::Font(10.0f));
    riseFaderLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextDim());
    addAndMakeVisible(riseFaderLabel);
    
    riseFaderValueLabel.setJustificationType(juce::Justification::centred);
    riseFaderValueLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    riseFaderValueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(riseFaderValueLabel);

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

    // v1.2.1: Set plugin window size (compact layout)
    setSize(780, 520);
    
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
        &octaveModeBox, &pitchRangeKnob, &pitchSpeedKnob, &riseFader,
        &riseFaderLabel, &riseFaderValueLabel, &pitchSubLabel,
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
    const int headerHeight = 50;
    const int leftPanelWidth = 70;
    const int rightPanelWidth = 70;
    
    // v1.2.0: Dark background
    g.fillAll(juce::Colour(0xff1A1A1A));
    
    // Draw background image in content area (if valid)
    if (backgroundImage.isValid()) {
        auto contentArea = juce::Rectangle<float>((float)leftPanelWidth, (float)headerHeight, 
                                                   (float)(getWidth() - leftPanelWidth - rightPanelWidth), 
                                                   (float)(getHeight() - headerHeight));
        g.setOpacity(0.3f);  // Subtle background
        g.drawImage(backgroundImage, contentArea, juce::RectanglePlacement::stretchToFit);
        g.setOpacity(1.0f);
    }
    
    // === HEADER SECTION v1.2.0 ===
    g.setColour(juce::Colour(0xff1F1F1F));
    g.fillRect(0, 0, getWidth(), headerHeight);
    
    // Draw header logo
    if (headerLogoImage.isValid()) {
        float logoScale = (float)(headerHeight - 16) / (float)headerLogoImage.getHeight();
        float logoW = headerLogoImage.getWidth() * logoScale;
        float logoH = headerLogoImage.getHeight() * logoScale;
        float logoX = (getWidth() - logoW) * 0.5f;
        float logoY = (headerHeight - logoH) * 0.5f;
        
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(headerLogoImage, 
                    juce::Rectangle<float>(logoX, logoY, logoW, logoH),
                    juce::RectanglePlacement::centred);
    }

    // === LEFT PANEL (TONE) ===
    g.setColour(juce::Colour(0xff1A1A1A));
    g.fillRect(0, headerHeight, leftPanelWidth, getHeight() - headerHeight);
    
    // === RIGHT PANEL (OUTPUT) ===
    g.fillRect(getWidth() - rightPanelWidth, headerHeight, rightPanelWidth, getHeight() - headerHeight);
    
    // === DIVIDER LINES v1.2.0 ===
    g.setColour(juce::Colour(0xff333333));
    
    // Vertical dividers
    g.drawLine((float)leftPanelWidth, (float)headerHeight, (float)leftPanelWidth, (float)getHeight(), 1.0f);
    g.drawLine((float)(getWidth() - rightPanelWidth), (float)headerHeight, 
               (float)(getWidth() - rightPanelWidth), (float)getHeight(), 1.0f);
    
    // v1.2.1: Horizontal divider between VOLTAGE and SWARM/FLOW
    int dividerY = 230;
    g.drawLine((float)leftPanelWidth, (float)dividerY, (float)(getWidth() - rightPanelWidth), (float)dividerY, 1.0f);
    
    // Vertical divider between SWARM and FLOW
    int centerX = getWidth() / 2;
    g.drawLine((float)centerX, (float)dividerY, (float)centerX, (float)(getHeight() - 85), 1.0f);
    
    // === SECTION TITLES v1.2.0 ===
    g.setColour(MetalLookAndFeel::getAccentOrange());
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    
    // VOLTAGE title (center top)
    int voltageX = leftPanelWidth + 10;
    int voltageWidth = getWidth() - leftPanelWidth - rightPanelWidth - 20;
    g.drawText("VOLTAGE", voltageX, headerHeight + 8, voltageWidth - 40, 18, juce::Justification::centred);
    
    // SWARM title (center bottom left)
    int swarmWidth = (getWidth() - leftPanelWidth - rightPanelWidth) / 2;
    g.drawText("SWARM", leftPanelWidth, dividerY + 8, swarmWidth - 40, 18, juce::Justification::centred);
    
    // FLOW title (center bottom right)
    g.drawText("FLOW", centerX, dividerY + 8, swarmWidth - 40, 18, juce::Justification::centred);
    
    // Version number (bottom right corner)
    g.setColour(MetalLookAndFeel::getTextDim());
    g.setFont(juce::Font(10.0f));
    g.drawText("v1.2.2", getWidth() - 60, getHeight() - 18, 50, 14, juce::Justification::centredRight);
}

void SwarmnesssAudioProcessorEditor::resized() {
    const int knobSize = 55;
    const int smallKnobSize = 50;
    const int comboHeight = 22;
    const int headerHeight = 50;
    const int leftPanelWidth = 70;
    const int rightPanelWidth = 70;
    const int faderWidth = 30;
    const int faderHeight = 100;
    const int powerButtonSize = 24;
    
    // === Preset Selector and Buttons (top-left in header) ===
    presetSelector.setBounds(10, 12, 100, 26);
    savePresetButton.setBounds(115, 12, 45, 26);
    exportPresetButton.setBounds(165, 12, 55, 26);
    importPresetButton.setBounds(225, 12, 55, 26);
    
    // === Info Button (top-right in header) ===
    infoButton.setBounds(getWidth() - 35, 12, 26, 26);
    
    // Info Panel (full screen overlay)
    infoPanel.setBounds(getLocalBounds());
    
    // === LEFT PANEL: TONE (3 vertical faders) ===
    {
        int baseX = 10;
        int startY = headerHeight + 25;
        int spacing = 120;
        
        // LOW CUT fader
        lowCutFader.setBounds(baseX + 5, startY, faderWidth, faderHeight);
        lowCutLabel.setBounds(baseX - 5, startY + faderHeight + 2, 50, 11);
        lowCutValueLabel.setBounds(baseX - 5, startY + faderHeight + 13, 50, 11);
        
        // HIGH CUT fader
        highCutFader.setBounds(baseX + 5, startY + spacing, faderWidth, faderHeight);
        highCutLabel.setBounds(baseX - 5, startY + spacing + faderHeight + 2, 50, 11);
        highCutValueLabel.setBounds(baseX - 5, startY + spacing + faderHeight + 13, 50, 11);
        
        // MID BOOST fader
        midBoostFader.setBounds(baseX + 5, startY + spacing * 2, faderWidth, faderHeight);
        midBoostLabel.setBounds(baseX - 8, startY + spacing * 2 + faderHeight + 2, 56, 11);
        midBoostValueLabel.setBounds(baseX - 5, startY + spacing * 2 + faderHeight + 13, 50, 11);
    }
    
    // === RIGHT PANEL: OUTPUT (3 vertical faders) ===
    {
        int baseX = getWidth() - rightPanelWidth + 10;
        int startY = headerHeight + 25;
        int spacing = 120;
        
        // MIX fader
        mixFader.setBounds(baseX + 5, startY, faderWidth, faderHeight);
        mixLabel.setBounds(baseX - 5, startY + faderHeight + 2, 50, 11);
        mixValueLabel.setBounds(baseX - 5, startY + faderHeight + 13, 50, 11);
        
        // DRIVE fader
        driveFader.setBounds(baseX + 5, startY + spacing, faderWidth, faderHeight);
        driveLabel.setBounds(baseX - 5, startY + spacing + faderHeight + 2, 50, 11);
        driveValueLabel.setBounds(baseX - 5, startY + spacing + faderHeight + 13, 50, 11);
        
        // VOLUME fader
        volumeFader.setBounds(baseX + 5, startY + spacing * 2, faderWidth, faderHeight);
        volumeLabel.setBounds(baseX - 5, startY + spacing * 2 + faderHeight + 2, 50, 11);
        volumeValueLabel.setBounds(baseX - 5, startY + spacing * 2 + faderHeight + 13, 50, 11);
    }
    
    // === CENTER TOP: VOLTAGE Section ===
    {
        int sectionX = leftPanelWidth + 10;
        int sectionWidth = getWidth() - leftPanelWidth - rightPanelWidth - 20;
        int baseY = headerHeight + 26;
        
        // Power button (top-right of section title)
        pitchBypassButton.setBounds(sectionX + sectionWidth - 30, headerHeight + 6, powerButtonSize, powerButtonSize);
        
        // Left side: PITCH
        int pitchX = sectionX + 5;
        pitchSubLabel.setBounds(pitchX, baseY, 180, 14);
        
        // Octave dropdown (v1.2.2: wider to show full text)
        octaveModeBox.setBounds(pitchX, baseY + 18, 85, comboHeight);
        
        // v1.2.1: RANGE, SPEED, RISE in a row
        int knobY = baseY + 46;
        int knobSpacing = 58;
        pitchRangeKnob.setBounds(pitchX, knobY, knobSize, knobSize + 22);
        pitchSpeedKnob.setBounds(pitchX + knobSpacing, knobY, knobSize, knobSize + 22);
        
        // RISE Vertical Fader (next to SPEED)
        int riseX = pitchX + knobSpacing * 2 + 10;
        riseFader.setBounds(riseX, knobY + 5, 28, 70);
        riseFaderLabel.setBounds(riseX - 3, knobY + 75, 35, 12);
        riseFaderValueLabel.setBounds(riseX - 8, knobY + 86, 45, 12);
        
        // Right side: MODULATION (v1.2.2: closer to PITCH for compact layout)
        int modX = sectionX + 260;  // Fixed offset for compact layout
        modulationSubLabel.setBounds(modX, baseY, 180, 14);
        
        // ANGER, RUSH, RATE knobs (same size as PITCH knobs)
        angerKnob.setBounds(modX, knobY, knobSize, knobSize + 22);
        rushKnob.setBounds(modX + knobSpacing, knobY, knobSize, knobSize + 22);
        modRateKnob.setBounds(modX + knobSpacing * 2, knobY, knobSize, knobSize + 22);
    }
    
    // === CENTER BOTTOM: SWARM + FLOW ===
    int dividerY = 230;
    int swarmFlowWidth = (getWidth() - leftPanelWidth - rightPanelWidth) / 2;
    
    // SWARM Section (left)
    {
        int baseX = leftPanelWidth + 10;
        int baseY = dividerY + 26;
        
        // Power button
        swarmBypassButton.setBounds(baseX + swarmFlowWidth - 42, dividerY + 6, powerButtonSize, powerButtonSize);
        
        // 3 knobs + DEEP toggle (uniform size)
        int knobY = baseY + 12;
        int spacing = 52;
        swarmDepthKnob.setBounds(baseX + 5, knobY, knobSize, knobSize + 22);
        swarmRateKnob.setBounds(baseX + 5 + spacing, knobY, knobSize, knobSize + 22);
        swarmMixKnob.setBounds(baseX + 5 + spacing * 2, knobY, knobSize, knobSize + 22);
        
        // DEEP toggle (inline)
        int toggleX = baseX + 5 + spacing * 3 + 8;
        deepModeButton.setBounds(toggleX, knobY + 12, 38, 22);
        deepModeLabel.setBounds(toggleX - 2, knobY + 36, 45, 14);
    }
    
    // FLOW Section (right)
    {
        int centerX = getWidth() / 2;
        int baseX = centerX + 10;
        int baseY = dividerY + 26;
        
        // Power button
        flowBypassButton.setBounds(baseX + swarmFlowWidth - 42, dividerY + 6, powerButtonSize, powerButtonSize);
        
        // 2 knobs (uniform size)
        int knobY = baseY + 12;
        int spacing = 60;
        flowAmountKnob.setBounds(baseX + 20, knobY, knobSize, knobSize + 22);
        flowSpeedKnob.setBounds(baseX + 20 + spacing, knobY, knobSize, knobSize + 22);
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
