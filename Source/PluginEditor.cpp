#include "PluginProcessor.h"
#include "PluginEditor.h"

SwarmnesssAudioProcessorEditor::SwarmnesssAudioProcessorEditor(SwarmnesssAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&metalLookAndFeel);

    // Load images from BinaryData
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::background_png, BinaryData::background_pngSize);
    logoImage = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    // Preset Panel (hidden by default, can be shown if needed)
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

    // === PITCH Section ===
    setupSectionLabel(pitchSectionLabel, "PITCH");
    
    addAndMakeVisible(octaveModeBox);
    octaveModeBox.addItem("-2 OCT", 1);
    octaveModeBox.addItem("-1 OCT", 2);
    octaveModeBox.addItem("0", 3);
    octaveModeBox.addItem("+1 OCT", 4);
    octaveModeBox.addItem("+2 OCT", 5);
    octaveModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "octaveMode", octaveModeBox);

    addAndMakeVisible(pitchRangeKnob);
    pitchRangeKnob.setValueSuffix("%");
    pitchRangeKnob.setValueMultiplier(100.0f);
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
    
    // RISE Label
    riseFaderLabel.setText("RISE", juce::dontSendNotification);
    riseFaderLabel.setJustificationType(juce::Justification::centred);
    riseFaderLabel.setFont(juce::Font(9.0f));
    riseFaderLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextLight());
    addAndMakeVisible(riseFaderLabel);
    
    // RISE Value Label
    riseFaderValueLabel.setJustificationType(juce::Justification::centred);
    riseFaderValueLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    riseFaderValueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    addAndMakeVisible(riseFaderValueLabel);

    addAndMakeVisible(pitchOnButton);
    pitchOnButton.setButtonText("");
    engageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "engage", pitchOnButton);

    // === MODULATION Section ===
    setupSectionLabel(modulationSectionLabel, "MODULATION");

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
    setupSectionLabel(toneSectionLabel, "TONE");

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
    setupSectionLabel(swarmSectionLabel, "SWARM");

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

    // Chorus Mode Toggle (Classic / Deep)
    addAndMakeVisible(chorusModeButton);
    chorusModeButton.setColour(juce::ToggleButton::textColourId, MetalLookAndFeel::getTextLight());
    chorusModeButton.setColour(juce::ToggleButton::tickColourId, MetalLookAndFeel::getAccentOrange());
    chorusModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "chorusMode", chorusModeButton);

    // === FLOW Section (Stutter/Gate) ===
    setupSectionLabel(flowSectionLabel, "FLOW");

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

    // Flow Mode Toggle (Smooth / Hard)
    addAndMakeVisible(flowModeButton);
    flowModeButton.setColour(juce::ToggleButton::textColourId, MetalLookAndFeel::getTextLight());
    flowModeButton.setColour(juce::ToggleButton::tickColourId, MetalLookAndFeel::getAccentOrange());
    flowModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "flowMode", flowModeButton);

    // === OUTPUT Section ===
    setupSectionLabel(outputSectionLabel, "OUTPUT");

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

    // Start timer for LED updates
    startTimerHz(30);

    // Set plugin window size to match prototype aspect ratio
    setSize(1000, 700);
}

SwarmnesssAudioProcessorEditor::~SwarmnesssAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SwarmnesssAudioProcessorEditor::setupSectionLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrange());
    label.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(label);
}

void SwarmnesssAudioProcessorEditor::timerCallback() {
    // Update bypass footswitch LED
    auto bypassed = *audioProcessor.getAPVTS().getRawParameterValue("globalBypass") > 0.5f;
    bypassFootswitch.setLEDState(bypassed ? FootswitchButton::DimRed : FootswitchButton::BrightRed);
    bypassFootswitch.setOn(!bypassed);
}

void SwarmnesssAudioProcessorEditor::drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) {
    const float cornerSize = 12.0f;  // Per spec: border-radius: 12px
    const float borderWidth = 1.5f;  // Per spec: 1.5px solid
    
    // Semi-transparent dark background (per spec: rgba(15, 15, 15, 0.88))
    g.setColour(juce::Colour(0xe00f0f0f));
    g.fillRoundedRectangle(bounds.toFloat(), cornerSize);
    
    // Orange glow effect (per spec: box-shadow: 0 0 6px rgba(255, 140, 0, 0.3))
    g.setColour(juce::Colour(0x4dFF8C00));  // #FF8C00 with 0.3 alpha
    g.drawRoundedRectangle(bounds.toFloat().expanded(2), cornerSize + 2, 3.0f);
    
    // Orange border (per spec: 1.5px solid #FF8C00)
    g.setColour(juce::Colour(0xffFF8C00));
    g.drawRoundedRectangle(bounds.toFloat().reduced(borderWidth * 0.5f), cornerSize, borderWidth);
    
    // Section title (per spec: white, bold, 14px, uppercase)
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText(title.toUpperCase(), bounds.getX(), bounds.getY() + 6, bounds.getWidth(), 18, 
               juce::Justification::centred, false);
}

void SwarmnesssAudioProcessorEditor::paint(juce::Graphics& g) {
    // Draw background image (scaled to fit)
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::stretchToFit);
    } else {
        g.fillAll(MetalLookAndFeel::getBackgroundDark());
    }
    
    // Dark overlay to make background less distracting (60% opacity black)
    g.setColour(juce::Colour(0x99000000));
    g.fillRect(getLocalBounds());

    // Draw logo at top center - preserve aspect ratio
    if (logoImage.isValid()) {
        const float originalAspect = (float)logoImage.getWidth() / (float)logoImage.getHeight();
        const int logoHeight = 55;
        const int logoWidth = (int)(logoHeight * originalAspect);
        const int logoX = (getWidth() - logoWidth) / 2;
        const int logoY = 12;
        g.drawImage(logoImage, logoX, logoY, logoWidth, logoHeight,
                    0, 0, logoImage.getWidth(), logoImage.getHeight());
    } else {
        // Fallback text logo with yellow-orange color
        g.setColour(MetalLookAndFeel::getAccentOrangeBright());
        g.setFont(juce::Font(32.0f, juce::Font::bold));
        g.drawText("SWARMNESS", 0, 15, getWidth(), 45, juce::Justification::centred);
    }

    // Version number
    g.setColour(MetalLookAndFeel::getTextDim());
    g.setFont(juce::Font(11.0f));
    g.drawText("v3.2.0", getWidth() - 70, 20, 60, 20, juce::Justification::centredRight);

    // Draw 6 section frames (v3.2.0 layout)
    // Top row: PITCH, MODULATION, TONE
    drawSectionFrame(g, juce::Rectangle<int>(15, 80, 310, 230), "PITCH");
    drawSectionFrame(g, juce::Rectangle<int>(340, 80, 310, 230), "MODULATION");
    drawSectionFrame(g, juce::Rectangle<int>(665, 80, 320, 230), "TONE");
    
    // Bottom row: SWARM, FLOW, OUTPUT
    drawSectionFrame(g, juce::Rectangle<int>(15, 325, 310, 180), "SWARM");
    drawSectionFrame(g, juce::Rectangle<int>(340, 325, 310, 180), "FLOW");
    drawSectionFrame(g, juce::Rectangle<int>(665, 325, 320, 180), "OUTPUT");
}

void SwarmnesssAudioProcessorEditor::resized() {
    const int knobSize = 70;
    const int smallKnobSize = 60;
    const int comboWidth = 90;
    const int comboHeight = 24;
    const int toggleSize = 28;
    
    // === Preset Selector and Buttons (top-left corner) ===
    presetSelector.setBounds(20, 20, 140, 28);
    savePresetButton.setBounds(165, 20, 50, 28);
    exportPresetButton.setBounds(220, 20, 60, 28);
    importPresetButton.setBounds(285, 20, 60, 28);
    
    // === Info Button (top-right corner) ===
    infoButton.setBounds(getWidth() - 45, 15, 30, 30);
    
    // Info Panel (full screen overlay)
    infoPanel.setBounds(getLocalBounds());
    
    // Section bounds
    const int topRowY = 80;
    const int bottomRowY = 325;

    // === PITCH Section (15, 80, 310, 230) ===
    {
        int baseX = 15;
        int baseY = topRowY;
        
        // Dropdown at top left of section
        octaveModeBox.setBounds(baseX + 15, baseY + 28, comboWidth, comboHeight);
        
        // ON/OFF Toggle next to dropdown
        pitchOnButton.setBounds(baseX + 115, baseY + 28, toggleSize + 40, toggleSize);
        
        // 2 knobs: RANGE, SPEED
        int knobY = baseY + 65;
        int spacing = 75;
        pitchRangeKnob.setBounds(baseX + 15, knobY, knobSize, knobSize + 30);
        pitchSpeedKnob.setBounds(baseX + 15 + spacing, knobY, knobSize, knobSize + 30);
        
        // RISE Vertical Fader (right side of PITCH section)
        int faderWidth = 40;
        int faderHeight = 130;
        int faderX = baseX + 230;
        int faderY = baseY + 45;
        riseFader.setBounds(faderX, faderY, faderWidth, faderHeight);
        riseFaderLabel.setBounds(faderX - 5, faderY + faderHeight, faderWidth + 10, 14);
        riseFaderValueLabel.setBounds(faderX - 5, faderY + faderHeight + 12, faderWidth + 10, 14);
    }

    // === MODULATION Section (340, 80, 310, 230) ===
    {
        int baseX = 340;
        int baseY = topRowY;
        
        // 3 knobs in a row
        int knobY = baseY + 70;
        int spacing = 95;
        angerKnob.setBounds(baseX + 20, knobY, knobSize, knobSize + 30);
        rushKnob.setBounds(baseX + 20 + spacing, knobY, knobSize, knobSize + 30);
        modRateKnob.setBounds(baseX + 20 + spacing * 2, knobY, knobSize, knobSize + 30);
    }

    // === TONE Section (665, 80, 320, 230) ===
    {
        int baseX = 665;
        int baseY = topRowY;
        
        // 3 knobs in a row
        int knobY = baseY + 70;
        int spacing = 100;
        lowCutKnob.setBounds(baseX + 15, knobY, knobSize, knobSize + 30);
        highCutKnob.setBounds(baseX + 15 + spacing, knobY, knobSize, knobSize + 30);
        midBoostKnob.setBounds(baseX + 15 + spacing * 2, knobY, knobSize, knobSize + 30);
    }

    // === SWARM Section (15, 325, 310, 180) ===
    {
        int baseX = 15;
        int baseY = bottomRowY;
        
        // 3 knobs + toggle
        int knobY = baseY + 40;
        int spacing = 70;
        swarmDepthKnob.setBounds(baseX + 15, knobY, smallKnobSize, smallKnobSize + 30);
        swarmRateKnob.setBounds(baseX + 15 + spacing, knobY, smallKnobSize, smallKnobSize + 30);
        swarmMixKnob.setBounds(baseX + 15 + spacing * 2, knobY, smallKnobSize, smallKnobSize + 30);
        
        // MODE toggle
        chorusModeButton.setBounds(baseX + 15 + spacing * 3 + 5, knobY + 20, toggleSize + 25, toggleSize);
    }

    // === FLOW Section (340, 325, 310, 180) ===
    {
        int baseX = 340;
        int baseY = bottomRowY;
        
        // 2 knobs + toggle
        int knobY = baseY + 40;
        int spacing = 90;
        flowAmountKnob.setBounds(baseX + 25, knobY, smallKnobSize, smallKnobSize + 30);
        flowSpeedKnob.setBounds(baseX + 25 + spacing, knobY, smallKnobSize, smallKnobSize + 30);
        
        // MODE toggle
        flowModeButton.setBounds(baseX + 25 + spacing * 2 + 10, knobY + 20, toggleSize + 25, toggleSize);
    }

    // === OUTPUT Section (665, 325, 320, 180) ===
    {
        int baseX = 665;
        int baseY = bottomRowY;
        
        // 3 knobs
        int knobY = baseY + 40;
        int spacing = 100;
        mixKnob.setBounds(baseX + 15, knobY, smallKnobSize, smallKnobSize + 30);
        driveKnob.setBounds(baseX + 15 + spacing, knobY, smallKnobSize, smallKnobSize + 30);
        volumeKnob.setBounds(baseX + 15 + spacing * 2, knobY, smallKnobSize, smallKnobSize + 30);
    }

    // === BYPASS Footswitch (bottom center) ===
    {
        int footWidth = 100;
        int footHeight = 100;
        int footX = (getWidth() - footWidth) / 2;
        int footY = 530;
        bypassFootswitch.setBounds(footX, footY, footWidth, footHeight);
    }
}

void SwarmnesssAudioProcessorEditor::refreshPresetList() {
    presetSelector.clear();
    auto& presetMgr = audioProcessor.getPresetManager();
    auto presets = presetMgr.getPresetList();
    int itemId = 1;
    for (const auto& name : presets) {
        presetSelector.addItem(name, itemId++);
    }
    int currentIdx = presets.indexOf(presetMgr.getCurrentPresetName());
    if (currentIdx >= 0) presetSelector.setSelectedItemIndex(currentIdx);
}
