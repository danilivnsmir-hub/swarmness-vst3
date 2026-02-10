#include "PluginProcessor.h"
#include "PluginEditor.h"

SwarmnesssAudioProcessorEditor::SwarmnesssAudioProcessorEditor(SwarmnesssAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&metalLookAndFeel);

    // Load background image 
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::background_png, BinaryData::background_pngSize);
    
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

    // === PITCH + MODULATION Combined Section ===
    
    // Power button for PITCH+MOD (uses "engage" parameter)
    addAndMakeVisible(pitchModPowerButton);
    pitchModPowerButton.setClickingTogglesState(true);
    engageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "engage", pitchModPowerButton);
    pitchModPowerButton.onClick = [this]() { updateSectionEnableStates(); };

    // PITCH subheader label
    pitchSubLabel.setText("PITCH", juce::dontSendNotification);
    pitchSubLabel.setJustificationType(juce::Justification::centred);
    pitchSubLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    pitchSubLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(pitchSubLabel);
    
    // MODULATION subheader label
    modulationSubLabel.setText("MODULATION", juce::dontSendNotification);
    modulationSubLabel.setJustificationType(juce::Justification::centred);
    modulationSubLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    modulationSubLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(modulationSubLabel);

    // PITCH controls
    addAndMakeVisible(octaveModeBox);
    octaveModeBox.addItem("-2 OCT", 1);
    octaveModeBox.addItem("-1 OCT", 2);
    octaveModeBox.addItem("0", 3);
    octaveModeBox.addItem("+1 OCT", 4);
    octaveModeBox.addItem("+2 OCT", 5);
    octaveModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "octaveMode", octaveModeBox);

    addAndMakeVisible(pitchRangeKnob);
    pitchRangeKnob.setValueSuffix(" st");
    pitchRangeKnob.setValueMultiplier(1.0f);  // Now 0-24 semitones directly
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
    riseFaderLabel.setFont(juce::Font(11.0f));  // Phase 1 UI: Increased from 9px to 11px
    riseFaderLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextLight());
    addAndMakeVisible(riseFaderLabel);
    
    riseFaderValueLabel.setJustificationType(juce::Justification::centred);
    riseFaderValueLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    riseFaderValueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(riseFaderValueLabel);

    // MODULATION controls
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

    // === TONE Section ===
    addAndMakeVisible(lowCutKnob);
    lowCutKnob.setValueSuffix("%");
    lowCutKnob.setValueMultiplier(100.0f);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowCut", lowCutKnob.getSlider());

    addAndMakeVisible(highCutKnob);
    highCutKnob.setValueSuffix("%");
    highCutKnob.setValueMultiplier(100.0f);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highCut", highCutKnob.getSlider());

    addAndMakeVisible(midBoostKnob);
    midBoostKnob.setValueSuffix("%");
    midBoostKnob.setValueMultiplier(100.0f);
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", midBoostKnob.getSlider());

    // === SWARM Section (Chorus) ===
    addAndMakeVisible(swarmPowerButton);
    swarmPowerButton.setClickingTogglesState(true);
    chorusEngageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "chorusEngage", swarmPowerButton);
    swarmPowerButton.onClick = [this]() { updateSectionEnableStates(); };

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

    addAndMakeVisible(chorusModeButton);
    chorusModeButton.setColour(juce::ToggleButton::textColourId, MetalLookAndFeel::getTextLight());
    chorusModeButton.setColour(juce::ToggleButton::tickColourId, MetalLookAndFeel::getAccentOrange());
    chorusModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "chorusMode", chorusModeButton);
    
    // "DEEP" label under toggle (Phase 3 UI)
    chorusModeLabel.setText("DEEP", juce::dontSendNotification);
    chorusModeLabel.setJustificationType(juce::Justification::centred);
    chorusModeLabel.setFont(juce::Font(11.0f));
    chorusModeLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(chorusModeLabel);

    // === FLOW Section (Stutter/Gate) ===
    addAndMakeVisible(flowPowerButton);
    flowPowerButton.setClickingTogglesState(true);
    flowEngageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "flowEngage", flowPowerButton);
    flowPowerButton.onClick = [this]() { updateSectionEnableStates(); };

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

    // flowModeButton removed - flowMode is always true (Hard mode)

    // === OUTPUT Section ===
    addAndMakeVisible(mixKnob);
    mixKnob.setValueSuffix("%");
    mixKnob.setValueMultiplier(100.0f);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixKnob.getSlider());

    addAndMakeVisible(volumeKnob);
    volumeKnob.setValueSuffix("%");
    volumeKnob.setValueMultiplier(100.0f);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", volumeKnob.getSlider());

    addAndMakeVisible(driveKnob);
    driveKnob.setValueSuffix("%");
    driveKnob.setValueMultiplier(100.0f);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "drive", driveKnob.getSlider());

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

    // v1.1.0: Set plugin window size (90px header + content + metal strip + footswitch)
    setSize(1000, 800);
    
    // Initial update of section enable states
    updateSectionEnableStates();
}

SwarmnesssAudioProcessorEditor::~SwarmnesssAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SwarmnesssAudioProcessorEditor::timerCallback() {
    // Update bypass footswitch LED
    auto bypassed = *audioProcessor.getAPVTS().getRawParameterValue("globalBypass") > 0.5f;
    bypassFootswitch.setLEDState(bypassed ? FootswitchButton::DimRed : FootswitchButton::BrightRed);
    bypassFootswitch.setOn(!bypassed);
    
    // Periodically update section enable states (in case parameters change externally)
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
    // PITCH + MODULATION section
    bool pitchEnabled = pitchModPowerButton.getToggleState();
    setSectionEnabled({
        &octaveModeBox, &pitchRangeKnob, &pitchSpeedKnob, &riseFader,
        &riseFaderLabel, &riseFaderValueLabel, &pitchSubLabel,
        &angerKnob, &rushKnob, &modRateKnob, &modulationSubLabel
    }, pitchEnabled);
    
    // SWARM section
    bool swarmEnabled = swarmPowerButton.getToggleState();
    setSectionEnabled({
        &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob, &chorusModeButton, &chorusModeLabel
    }, swarmEnabled);
    
    // FLOW section
    bool flowEnabled = flowPowerButton.getToggleState();
    setSectionEnabled({
        &flowAmountKnob, &flowSpeedKnob
    }, flowEnabled);
}

void SwarmnesssAudioProcessorEditor::drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) {
    const float cornerSize = 10.0f;  // v1.1.0: 10px border-radius
    const float borderWidth = 2.0f;   // v1.1.0: 2px orange border
    
    // v1.1.0: Drop shadow effect
    juce::DropShadow shadow(juce::Colour(0x80000000), 12, juce::Point<int>(0, 4));
    juce::Path shadowPath;
    shadowPath.addRoundedRectangle(bounds.toFloat(), cornerSize);
    shadow.drawForPath(g, shadowPath);
    
    // v1.1.0: Section background gradient #2E2E2E (top) → #262626 (bottom) - "pedal" style
    juce::ColourGradient bgGrad(juce::Colour(0xff2E2E2E), bounds.toFloat().getCentreX(), bounds.toFloat().getY(),
                                 juce::Colour(0xff262626), bounds.toFloat().getCentreX(), bounds.toFloat().getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds.toFloat(), cornerSize);
    
    // v1.1.0: Inner glow - 1px blur orange inside
    g.setColour(juce::Colour(0x30FF9500));
    g.drawRoundedRectangle(bounds.toFloat().reduced(3.0f), cornerSize - 2, 2.0f);
    
    // v1.1.0: Orange border 2px #FF9500
    g.setColour(juce::Colour(0xffFF9500));
    g.drawRoundedRectangle(bounds.toFloat().reduced(borderWidth * 0.5f), cornerSize, borderWidth);
    
    // v1.1.0: Bevel effect - 1px #3A3A3A highlight at top
    g.setColour(juce::Colour(0xff3A3A3A));
    g.drawLine(bounds.toFloat().getX() + cornerSize + 2, bounds.toFloat().getY() + 3.0f,
               bounds.toFloat().getRight() - cornerSize - 2, bounds.toFloat().getY() + 3.0f, 1.0f);
    
    // v1.1.0: Bevel effect - 1px #1A1A1A shadow at bottom
    g.setColour(juce::Colour(0xff1A1A1A));
    g.drawLine(bounds.toFloat().getX() + cornerSize + 2, bounds.toFloat().getBottom() - 3.0f,
               bounds.toFloat().getRight() - cornerSize - 2, bounds.toFloat().getBottom() - 3.0f, 1.0f);
    
    // v1.1.0: Section title - orange #FF9500
    g.setColour(juce::Colour(0xffFF9500));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText(title.toUpperCase(), bounds.getX(), bounds.getY() + 6, bounds.getWidth(), 18, 
               juce::Justification::centred, false);
}

void SwarmnesssAudioProcessorEditor::drawCombinedSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds) {
    const float cornerSize = 10.0f;  // v1.1.0: 10px border-radius
    const float borderWidth = 2.0f;   // v1.1.0: 2px orange border
    
    // v1.1.0: Drop shadow effect
    juce::DropShadow shadow(juce::Colour(0x80000000), 12, juce::Point<int>(0, 4));
    juce::Path shadowPath;
    shadowPath.addRoundedRectangle(bounds.toFloat(), cornerSize);
    shadow.drawForPath(g, shadowPath);
    
    // v1.1.0: Section background gradient #2E2E2E (top) → #262626 (bottom) - "pedal" style
    juce::ColourGradient bgGrad(juce::Colour(0xff2E2E2E), bounds.toFloat().getCentreX(), bounds.toFloat().getY(),
                                 juce::Colour(0xff262626), bounds.toFloat().getCentreX(), bounds.toFloat().getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds.toFloat(), cornerSize);
    
    // v1.1.0: Inner glow - 1px blur orange inside
    g.setColour(juce::Colour(0x30FF9500));
    g.drawRoundedRectangle(bounds.toFloat().reduced(3.0f), cornerSize - 2, 2.0f);
    
    // v1.1.0: Orange border 2px #FF9500
    g.setColour(juce::Colour(0xffFF9500));
    g.drawRoundedRectangle(bounds.toFloat().reduced(borderWidth * 0.5f), cornerSize, borderWidth);
    
    // v1.1.0: Bevel effect - 1px #3A3A3A highlight at top
    g.setColour(juce::Colour(0xff3A3A3A));
    g.drawLine(bounds.toFloat().getX() + cornerSize + 2, bounds.toFloat().getY() + 3.0f,
               bounds.toFloat().getRight() - cornerSize - 2, bounds.toFloat().getY() + 3.0f, 1.0f);
    
    // v1.1.0: Bevel effect - 1px #1A1A1A shadow at bottom
    g.setColour(juce::Colour(0xff1A1A1A));
    g.drawLine(bounds.toFloat().getX() + cornerSize + 2, bounds.toFloat().getBottom() - 3.0f,
               bounds.toFloat().getRight() - cornerSize - 2, bounds.toFloat().getBottom() - 3.0f, 1.0f);
    
    // v1.1.0: Combined title "VOLTAGE" - orange #FF9500
    g.setColour(juce::Colour(0xffFF9500));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText("VOLTAGE", bounds.getX(), bounds.getY() + 6, bounds.getWidth() - 45, 18, 
               juce::Justification::centred, false);
}

void SwarmnesssAudioProcessorEditor::paint(juce::Graphics& g) {
    const int headerHeight = 90;  // v1.1.0: Increased from 70 to 90px
    
    // v1.1.0: Draw clean background (gradient or image)
    if (backgroundImage.isValid()) {
        auto contentArea = juce::Rectangle<float>(0, (float)headerHeight, (float)getWidth(), (float)(getHeight() - headerHeight));
        g.drawImage(backgroundImage, contentArea, juce::RectanglePlacement::stretchToFit);
    } else {
        // v1.1.0: Fallback gradient #1A1A1A → #0D0D0D
        juce::ColourGradient bgGrad(juce::Colour(0xff1A1A1A), 0, (float)headerHeight,
                                     juce::Colour(0xff0D0D0D), 0, (float)getHeight(), false);
        g.setGradientFill(bgGrad);
        g.fillRect(0, headerHeight, getWidth(), getHeight() - headerHeight);
    }
    
    // v1.1.0: Subtle dark overlay (less opacity to show cleaner background)
    g.setColour(juce::Colour(0x60000000));
    g.fillRect(0, headerHeight, getWidth(), getHeight() - headerHeight);

    // === HEADER SECTION v1.1.0 ===
    // Header background - gradient #252525 (top) → #1A1A1A (bottom)
    juce::ColourGradient headerGrad(juce::Colour(0xff252525), 0, 0,
                                     juce::Colour(0xff1A1A1A), 0, (float)headerHeight, false);
    g.setGradientFill(headerGrad);
    g.fillRect(0, 0, getWidth(), headerHeight);
    
    // v1.1.0: Orange accent line 2px at bottom of header
    g.setColour(juce::Colour(0xffFF9500));
    g.fillRect(0, headerHeight - 2, getWidth(), 2);
    
    // v1.1.0: Draw header logo centered, scaled to fit fully
    if (headerLogoImage.isValid()) {
        float logoScale = (float)(headerHeight - 20) / (float)headerLogoImage.getHeight();
        float logoW = headerLogoImage.getWidth() * logoScale;
        float logoH = headerLogoImage.getHeight() * logoScale;
        float logoX = (getWidth() - logoW) * 0.5f;
        float logoY = (headerHeight - logoH) * 0.5f;
        
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(headerLogoImage, 
                    juce::Rectangle<float>(logoX, logoY, logoW, logoH),
                    juce::RectanglePlacement::centred);
    }

    // v1.1.0: Version number (top right in header)
    g.setColour(MetalLookAndFeel::getTextDim());
    g.setFont(juce::Font(11.0f));
    g.drawText("v1.1.0", getWidth() - 70, 35, 60, 20, juce::Justification::centredRight);

    // Draw section frames (shifted down by headerHeight)
    int topRowY = headerHeight + 10;      // 100
    int bottomRowY = headerHeight + 255;  // 345
    
    // Top row: PITCH+MODULATION (combined large), TONE
    drawCombinedSectionFrame(g, juce::Rectangle<int>(15, topRowY, 640, 230));
    drawSectionFrame(g, juce::Rectangle<int>(670, topRowY, 315, 230), "TONE");
    
    // Bottom row: SWARM, FLOW, OUTPUT
    drawSectionFrame(g, juce::Rectangle<int>(15, bottomRowY, 310, 180), "SWARM");
    drawSectionFrame(g, juce::Rectangle<int>(340, bottomRowY, 310, 180), "FLOW");
    drawSectionFrame(g, juce::Rectangle<int>(665, bottomRowY, 320, 180), "OUTPUT");
    
    // === v1.1.0: METAL STRIP between sections and footswitch ===
    int metalStripY = bottomRowY + 190;  // After sections
    int metalStripHeight = 25;
    
    // Brushed metal gradient: #3A3A3A → #2A2A2A → #3A3A3A
    juce::ColourGradient metalGrad(juce::Colour(0xff3A3A3A), 0, (float)metalStripY,
                                    juce::Colour(0xff2A2A2A), 0, (float)(metalStripY + metalStripHeight / 2), false);
    g.setGradientFill(metalGrad);
    g.fillRect(0, metalStripY, getWidth(), metalStripHeight / 2);
    
    juce::ColourGradient metalGrad2(juce::Colour(0xff2A2A2A), 0, (float)(metalStripY + metalStripHeight / 2),
                                     juce::Colour(0xff3A3A3A), 0, (float)(metalStripY + metalStripHeight), false);
    g.setGradientFill(metalGrad2);
    g.fillRect(0, metalStripY + metalStripHeight / 2, getWidth(), metalStripHeight / 2);
    
    // v1.1.0: Subtle horizontal lines for brushed metal texture
    g.setColour(juce::Colour(0x15FFFFFF));
    for (int i = 0; i < metalStripHeight; i += 2) {
        g.drawLine(0.0f, (float)(metalStripY + i), (float)getWidth(), (float)(metalStripY + i), 0.5f);
    }
}

void SwarmnesssAudioProcessorEditor::resized() {
    const int knobSize = 70;
    const int smallKnobSize = 60;
    const int comboHeight = 24;
    const int toggleSize = 28;
    const int powerButtonSize = 30;
    const int headerHeight = 90;  // v1.1.0: Increased from 70 to 90px
    
    // === Preset Selector and Buttons (top-left in header) v1.1.0 ===
    presetSelector.setBounds(20, 30, 140, 28);
    savePresetButton.setBounds(165, 30, 50, 28);
    exportPresetButton.setBounds(220, 30, 60, 28);
    importPresetButton.setBounds(285, 30, 60, 28);
    
    // === Info Button (top-right in header) v1.1.0 ===
    infoButton.setBounds(getWidth() - 45, 30, 30, 30);
    
    // Info Panel (full screen overlay)
    infoPanel.setBounds(getLocalBounds());
    
    // Section bounds (shifted down by headerHeight)
    const int topRowY = headerHeight + 10;     // 80
    const int bottomRowY = headerHeight + 255;  // 325

    // === PITCH + MODULATION Combined Section (15, 80, 640, 230) ===
    {
        int baseX = 15;
        int baseY = topRowY;
        int sectionWidth = 640;
        
        // Power button in top-right corner of combined section
        pitchModPowerButton.setBounds(baseX + sectionWidth - 40, baseY + 8, powerButtonSize, powerButtonSize);
        
        // Left side: PITCH controls
        int pitchX = baseX + 15;
        pitchSubLabel.setBounds(pitchX, baseY + 30, 200, 16);
        
        // Dropdown for octave
        octaveModeBox.setBounds(pitchX, baseY + 50, 85, comboHeight);
        
        // 2 knobs: RANGE, SPEED (Phase 1 UI: 85px spacing)
        int knobY = baseY + 85;
        pitchRangeKnob.setBounds(pitchX, knobY, knobSize, knobSize + 30);
        pitchSpeedKnob.setBounds(pitchX + 85, knobY, knobSize, knobSize + 30);
        
        // RISE Vertical Fader
        int faderWidth = 40;
        int faderHeight = 110;
        int faderX = pitchX + 175;
        int faderY = baseY + 55;
        riseFader.setBounds(faderX, faderY, faderWidth, faderHeight);
        riseFaderLabel.setBounds(faderX - 5, faderY + faderHeight, faderWidth + 10, 14);
        riseFaderValueLabel.setBounds(faderX - 5, faderY + faderHeight + 12, faderWidth + 10, 14);
        
        // Right side: MODULATION controls
        int modX = baseX + 320;
        modulationSubLabel.setBounds(modX, baseY + 30, 280, 16);
        
        // 3 knobs: ANGER, RUSH, RATE (Phase 1 UI: 85px spacing)
        int modKnobY = baseY + 85;
        int spacing = 85;
        angerKnob.setBounds(modX, modKnobY, knobSize, knobSize + 30);
        rushKnob.setBounds(modX + spacing, modKnobY, knobSize, knobSize + 30);
        modRateKnob.setBounds(modX + spacing * 2, modKnobY, knobSize, knobSize + 30);
    }

    // === TONE Section (670, 80, 315, 230) ===
    {
        int baseX = 670;
        int baseY = topRowY;
        
        // 3 knobs in a row (Phase 1 UI: 85px spacing)
        int knobY = baseY + 70;
        int spacing = 85;
        lowCutKnob.setBounds(baseX + 25, knobY, knobSize, knobSize + 30);
        highCutKnob.setBounds(baseX + 25 + spacing, knobY, knobSize, knobSize + 30);
        midBoostKnob.setBounds(baseX + 25 + spacing * 2, knobY, knobSize, knobSize + 30);
    }

    // === SWARM Section (15, 325, 310, 180) ===
    {
        int baseX = 15;
        int baseY = bottomRowY;
        
        // Power button in top-right corner
        swarmPowerButton.setBounds(baseX + 310 - 40, baseY + 8, powerButtonSize, powerButtonSize);
        
        // 3 knobs + toggle (Phase 1 UI: 85px spacing for small knobs adjusted to 75px)
        int knobY = baseY + 45;
        int spacing = 75;  // Smaller spacing for smallKnobSize
        swarmDepthKnob.setBounds(baseX + 15, knobY, smallKnobSize, smallKnobSize + 30);
        swarmRateKnob.setBounds(baseX + 15 + spacing, knobY, smallKnobSize, smallKnobSize + 30);
        swarmMixKnob.setBounds(baseX + 15 + spacing * 2, knobY, smallKnobSize, smallKnobSize + 30);
        
        // MODE toggle + DEEP label (Phase 3 UI)
        int toggleX = baseX + 15 + spacing * 3;
        int toggleY = knobY + 15;
        chorusModeButton.setBounds(toggleX, toggleY, toggleSize + 25, toggleSize);
        chorusModeLabel.setBounds(toggleX, toggleY + toggleSize + 2, toggleSize + 25, 14);
    }

    // === FLOW Section (340, 325, 310, 180) ===
    {
        int baseX = 340;
        int baseY = bottomRowY;
        
        // Power button in top-right corner
        flowPowerButton.setBounds(baseX + 310 - 40, baseY + 8, powerButtonSize, powerButtonSize);
        
        // 2 knobs (flowModeButton removed - flowMode is always Hard) (Phase 1 UI: 85px spacing)
        int knobY = baseY + 45;
        int spacing = 85;
        flowAmountKnob.setBounds(baseX + 25, knobY, smallKnobSize, smallKnobSize + 30);
        flowSpeedKnob.setBounds(baseX + 25 + spacing, knobY, smallKnobSize, smallKnobSize + 30);
    }

    // === OUTPUT Section (665, 325, 320, 180) ===
    {
        int baseX = 665;
        int baseY = bottomRowY;
        
        // 3 knobs (no power button for OUTPUT) (Phase 1 UI: 85px spacing)
        int knobY = baseY + 45;
        int spacing = 85;
        mixKnob.setBounds(baseX + 25, knobY, smallKnobSize, smallKnobSize + 30);
        driveKnob.setBounds(baseX + 25 + spacing, knobY, smallKnobSize, smallKnobSize + 30);
        volumeKnob.setBounds(baseX + 25 + spacing * 2, knobY, smallKnobSize, smallKnobSize + 30);
    }

    // === BYPASS Footswitch (bottom center, below metal strip) v1.1.0 ===
    {
        int footWidth = 100;
        int footHeight = 100;
        int footX = (getWidth() - footWidth) / 2;
        int footY = headerHeight + 475;  // Below metal strip (90 + 10 + 230 + 180 + 25 + pad)
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
