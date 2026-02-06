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
    // addAndMakeVisible(*presetPanel); // Uncomment if needed
    
    // === Preset Dropdown (top-left) ===
    addAndMakeVisible(presetSelector);
    auto& presetMgr = audioProcessor.getPresetManager();
    auto presets = presetMgr.getPresetList();
    int itemId = 1;
    for (const auto& name : presets) {
        presetSelector.addItem(name, itemId++);
    }
    // Set current preset
    int currentIdx = presets.indexOf(presetMgr.getCurrentPresetName());
    if (currentIdx >= 0) presetSelector.setSelectedItemIndex(currentIdx);
    presetSelector.onChange = [this]() {
        auto selectedName = presetSelector.getText();
        audioProcessor.getPresetManager().loadPreset(selectedName);
    };

    // === VOLTAGE Section (maps to pitch/modulation controls) ===
    setupSectionLabel(voltageSectionLabel, "VOLTAGE");
    
    addAndMakeVisible(grainKnob);  // Connected to panic
    panicAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "panic", grainKnob.getSlider());

    addAndMakeVisible(pitchKnob);  // Connected to chaos
    chaosAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chaos", pitchKnob.getSlider());

    addAndMakeVisible(driftKnob);  // Connected to rise
    riseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "rise", driftKnob.getSlider());

    addAndMakeVisible(octaveModeBox);
    octaveModeBox.addItem("-2 OCT", 1);
    octaveModeBox.addItem("-1 OCT", 2);
    octaveModeBox.addItem("+1 OCT", 3);
    octaveModeBox.addItem("+2 OCT", 4);
    octaveModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "octaveMode", octaveModeBox);

    // Hidden engage button
    engageButton.setVisible(false);
    addAndMakeVisible(engageButton);
    engageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "engage", engageButton);

    // === PULSE Section (maps to slide controls) ===
    setupSectionLabel(pulseSectionLabel, "PULSE");

    addAndMakeVisible(slideTimeKnob);
    slideTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "slideTime", slideTimeKnob.getSlider());

    addAndMakeVisible(slideRangeKnob);
    slideRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "slideRange", slideRangeKnob.getSlider());

    addAndMakeVisible(slideOnButton);
    slideOnButton.setButtonText("");
    autoSlideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoSlide", slideOnButton);

    // Hidden slide controls
    slidePositionKnob.setVisible(false);
    addAndMakeVisible(slidePositionKnob);
    slidePositionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "slidePosition", slidePositionKnob.getSlider());

    slideDirectionBox.setVisible(false);
    addAndMakeVisible(slideDirectionBox);
    slideDirectionBox.addItem("Up", 1);
    slideDirectionBox.addItem("Down", 2);
    slideDirectionBox.addItem("Both", 3);
    slideDirectionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "slideDirection", slideDirectionBox);

    autoSlideButton.setVisible(false);
    addAndMakeVisible(autoSlideButton);

    slideReturnButton.setVisible(false);
    addAndMakeVisible(slideReturnButton);
    slideReturnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "slideReturn", slideReturnButton);

    // Hidden random controls
    randomRangeKnob.setVisible(false);
    addAndMakeVisible(randomRangeKnob);
    randomRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRange", randomRangeKnob.getSlider());

    randomRateKnob.setVisible(false);
    addAndMakeVisible(randomRateKnob);
    randomRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomRate", randomRateKnob.getSlider());

    randomSmoothKnob.setVisible(false);
    addAndMakeVisible(randomSmoothKnob);
    randomSmoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "randomSmooth", randomSmoothKnob.getSlider());

    randomModeBox.setVisible(false);
    addAndMakeVisible(randomModeBox);
    randomModeBox.addItem("Jump", 1);
    randomModeBox.addItem("Glide", 2);
    randomModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "randomMode", randomModeBox);

    // === HIVE FILTER Section ===
    setupSectionLabel(hiveFilterSectionLabel, "HIVE FILTER");

    addAndMakeVisible(cutoffKnob);  // Connected to lowCut
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowCut", cutoffKnob.getSlider());

    addAndMakeVisible(resonanceKnob);  // Connected to highCut
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highCut", resonanceKnob.getSlider());

    addAndMakeVisible(midBoostKnob);  // Connected to saturation
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", midBoostKnob.getSlider());

    // === SWARM Section (Chorus) ===
    setupSectionLabel(swarmSectionLabel, "SWARM");

    addAndMakeVisible(chorusDepthKnob);
    chorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusDepth", chorusDepthKnob.getSlider());

    addAndMakeVisible(chorusRateKnob);
    chorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusRate", chorusRateKnob.getSlider());

    addAndMakeVisible(wowFlutterKnob);  // Connected to chorusMix
    chorusMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "chorusMix", wowFlutterKnob.getSlider());

    // Hidden chorus mode
    chorusModeBox.setVisible(false);
    addAndMakeVisible(chorusModeBox);
    chorusModeBox.addItem("Classic", 1);
    chorusModeBox.addItem("Deep", 2);
    chorusModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "chorusMode", chorusModeBox);

    // === FLOW Section ===
    setupSectionLabel(flowSectionLabel, "FLOW");

    addAndMakeVisible(flowAmountKnob);  // Connected to pulseRate
    pulseRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "pulseRate", flowAmountKnob.getSlider());

    addAndMakeVisible(flowSpeedKnob);  // Connected to pulseProbability
    pulseProbabilityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "pulseProbability", flowSpeedKnob.getSlider());

    addAndMakeVisible(flowModeButton);
    flowModeButton.setButtonText("");

    // Hidden flow controls
    flowModeBox.setVisible(false);
    addAndMakeVisible(flowModeBox);
    flowModeBox.addItem("Static", 1);
    flowModeBox.addItem("Pulse", 2);
    flowModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "flowMode", flowModeBox);

    speedKnob.setVisible(false);
    addAndMakeVisible(speedKnob);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "speed", speedKnob.getSlider());

    // === OUTPUT Section ===
    setupSectionLabel(outputSectionLabel, "OUTPUT");

    addAndMakeVisible(mixKnob);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixKnob.getSlider());

    addAndMakeVisible(driveKnob);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "drive", driveKnob.getSlider());

    addAndMakeVisible(outputLevelKnob);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", outputLevelKnob.getSlider());

    // === BYPASS Footswitch ===
    // Logic: isOn=true means effect is ACTIVE (bypass OFF), isOn=false means BYPASSED
    addAndMakeVisible(bypassFootswitch);
    bypassFootswitch.onClick = [this](bool isOn) {
        auto* param = audioProcessor.getAPVTS().getParameter("globalBypass");
        if (param) {
            // When footswitch is ON (LED lit), bypass should be OFF (0.0)
            // When footswitch is OFF (LED dim), bypass should be ON (1.0)
            param->setValueNotifyingHost(isOn ? 0.0f : 1.0f);
        }
    };

    // Hidden pulse footswitch
    pulseFootswitch.setVisible(false);
    addAndMakeVisible(pulseFootswitch);

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
    label.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentRed());
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
    const float cornerSize = 4.0f;
    const float borderWidth = 2.0f;
    
    // Semi-transparent dark background
    g.setColour(juce::Colour(0x99000000));
    g.fillRoundedRectangle(bounds.toFloat(), cornerSize);
    
    // Red border
    g.setColour(MetalLookAndFeel::getAccentRed());
    g.drawRoundedRectangle(bounds.toFloat().reduced(borderWidth * 0.5f), cornerSize, borderWidth);
    
    // Section title
    g.setColour(MetalLookAndFeel::getAccentRed());
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(title, bounds.getX() + 10, bounds.getY() + 4, bounds.getWidth() - 20, 18, 
               juce::Justification::centredLeft, false);
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
        // Fallback text logo
        g.setColour(MetalLookAndFeel::getTextLight());
        g.setFont(juce::Font(32.0f, juce::Font::bold));
        g.drawText("SWARMNESS", 0, 15, getWidth(), 45, juce::Justification::centred);
    }

    // Version number
    g.setColour(MetalLookAndFeel::getTextDim());
    g.setFont(juce::Font(11.0f));
    g.drawText("v2.2.2", getWidth() - 70, 20, 60, 20, juce::Justification::centredRight);

    // Draw section frames
    // Top row
    drawSectionFrame(g, juce::Rectangle<int>(15, 80, 310, 230), "VOLTAGE");
    drawSectionFrame(g, juce::Rectangle<int>(340, 80, 310, 230), "PULSE");
    drawSectionFrame(g, juce::Rectangle<int>(665, 80, 320, 230), "HIVE FILTER");
    
    // Bottom row
    drawSectionFrame(g, juce::Rectangle<int>(15, 325, 310, 200), "SWARM");
    drawSectionFrame(g, juce::Rectangle<int>(340, 325, 310, 200), "FLOW");
    drawSectionFrame(g, juce::Rectangle<int>(665, 325, 320, 200), "OUTPUT");

    // Bypass label under footswitch
    g.setColour(MetalLookAndFeel::getTextLight());
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("BYPASS", getWidth()/2 - 50, 640, 100, 20, juce::Justification::centred);
}

void SwarmnesssAudioProcessorEditor::resized() {
    const int knobSize = 75;
    const int smallKnobSize = 70;
    const int comboWidth = 100;
    const int comboHeight = 24;
    const int toggleSize = 28;
    
    // === Preset Selector (top-left corner) ===
    presetSelector.setBounds(20, 20, 150, 28);
    
    // Section bounds
    const int topRowY = 80;
    const int bottomRowY = 325;
    const int sectionHeight = 230;
    const int bottomSectionHeight = 200;

    // === VOLTAGE Section (15, 80, 310, 230) ===
    {
        int baseX = 15;
        int baseY = topRowY;
        
        // Dropdown at top left of section
        octaveModeBox.setBounds(baseX + 20, baseY + 28, comboWidth, comboHeight);
        
        // 3 knobs in a row
        int knobY = baseY + 70;
        int spacing = 95;
        grainKnob.setBounds(baseX + 20, knobY, knobSize, knobSize + 18);
        pitchKnob.setBounds(baseX + 20 + spacing, knobY, knobSize, knobSize + 18);
        driftKnob.setBounds(baseX + 20 + spacing * 2, knobY, knobSize, knobSize + 18);
    }

    // === PULSE Section (340, 80, 310, 230) ===
    {
        int baseX = 340;
        int baseY = topRowY;
        
        // 2 knobs + toggle
        int knobY = baseY + 70;
        int spacing = 95;
        slideTimeKnob.setBounds(baseX + 20, knobY, knobSize, knobSize + 18);
        slideRangeKnob.setBounds(baseX + 20 + spacing, knobY, knobSize, knobSize + 18);
        
        // Toggle
        slideOnButton.setBounds(baseX + 20 + spacing * 2 + 15, knobY + 20, toggleSize + 20, toggleSize);
    }

    // === HIVE FILTER Section (665, 80, 320, 230) ===
    {
        int baseX = 665;
        int baseY = topRowY;
        
        // 3 knobs in a row
        int knobY = baseY + 70;
        int spacing = 100;
        cutoffKnob.setBounds(baseX + 15, knobY, knobSize, knobSize + 18);
        resonanceKnob.setBounds(baseX + 15 + spacing, knobY, knobSize, knobSize + 18);
        midBoostKnob.setBounds(baseX + 15 + spacing * 2, knobY, knobSize, knobSize + 18);
    }

    // === SWARM Section (15, 325, 310, 200) ===
    {
        int baseX = 15;
        int baseY = bottomRowY;
        
        // 3 knobs in a row
        int knobY = baseY + 50;
        int spacing = 95;
        chorusDepthKnob.setBounds(baseX + 20, knobY, knobSize, knobSize + 18);
        chorusRateKnob.setBounds(baseX + 20 + spacing, knobY, knobSize, knobSize + 18);
        wowFlutterKnob.setBounds(baseX + 20 + spacing * 2, knobY, knobSize, knobSize + 18);
    }

    // === FLOW Section (340, 325, 310, 200) ===
    {
        int baseX = 340;
        int baseY = bottomRowY;
        
        // 2 knobs + toggle
        int knobY = baseY + 50;
        int spacing = 95;
        flowAmountKnob.setBounds(baseX + 20, knobY, knobSize, knobSize + 18);
        flowSpeedKnob.setBounds(baseX + 20 + spacing, knobY, knobSize, knobSize + 18);
        
        // Toggle
        flowModeButton.setBounds(baseX + 20 + spacing * 2 + 15, knobY + 20, toggleSize + 20, toggleSize);
    }

    // === OUTPUT Section (665, 325, 320, 200) ===
    {
        int baseX = 665;
        int baseY = bottomRowY;
        
        // 3 knobs in a row
        int knobY = baseY + 50;
        int spacing = 100;
        mixKnob.setBounds(baseX + 15, knobY, knobSize, knobSize + 18);
        driveKnob.setBounds(baseX + 15 + spacing, knobY, knobSize, knobSize + 18);
        outputLevelKnob.setBounds(baseX + 15 + spacing * 2, knobY, knobSize, knobSize + 18);
    }

    // === BYPASS Footswitch (bottom center) ===
    {
        int footWidth = 90;
        int footHeight = 90;
        int footX = (getWidth() - footWidth) / 2;
        int footY = 545;
        bypassFootswitch.setBounds(footX, footY, footWidth, footHeight);
    }
}
