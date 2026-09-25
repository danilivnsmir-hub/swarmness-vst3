#include "PluginEditor.h"

using namespace Theme;

namespace
{
    juce::RangedAudioParameter& param (juce::AudioProcessorValueTreeState& s, const char* id)
    {
        auto* p = s.getParameter (id);
        jassert (p != nullptr);
        return *p;
    }
}

//==============================================================================
MainPanel::MainPanel (SwarmnessAudioProcessor& p)
    : processor (p),
      state (p.getAPVTS()),
      presetBar (p.getPresetManager()),
      switchModeSelector (param (state, ParamIDs::switchMode), { "MOMENTARY", "LATCH" }),
      oct1Switch   (param (state, ParamIDs::oct1),      "+1 OCT", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      oct2Switch   (param (state, ParamIDs::oct2),      "+2 OCT", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      magicSwitch  (param (state, ParamIDs::magicHold), "VENOM",  juce::Colour (0xffb46bff), false, [this] { return footswitchesMomentary(); }),
      bypassSwitch (param (state, ParamIDs::bypass),    "ON",     Colours::ledRed,  true,  nullptr)
{
    using namespace ParamIDs;

    logo = juce::ImageCache::getFromMemory (BinaryData::header_logo_png, BinaryData::header_logo_pngSize);

    backdrop.painter = [this] (juce::Graphics& g) { paintBackdrop (g); };
    backdrop.setInterceptsMouseClicks (false, false);
    backdrop.setBufferedToImage (true);
    backdrop.setOpaque (true);
    addAndMakeVisible (backdrop);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (switchModeSelector);
    switchModeSelector.setTooltip ("Footswitches: MOMENTARY = active only while held, LATCH = click on / click off");
    addAndMakeVisible (infoButton);
    infoButton.setTooltip ("Help");
    infoButton.onClick = [this] { infoOverlay.setVisible (true); infoOverlay.toFront (false); };

    // NOISE
    attachButton (downToggle, noiseDown, "DIVE: the octave footswitches shift DOWN (drop-tune) instead of up");
    riseKnob .attach (state, rise,  "Time to glide into the octave (and back when released)");
    panicKnob.attach (state, panic, "ANGER: detunes the shifted signal against a second voice - dissonance, beating, sour clusters");
    chaosKnob.attach (state, chaos, "FRENZY: random pitch jumps around the octave - wider and faster as you turn it up");
    speedKnob.attach (state, speed, "BUZZ: all-pass feedback + amplitude modulation - slow phasing up to metallic ring-mod shrieks");
    for (auto* c : std::initializer_list<juce::Component*> { &riseKnob, &panicKnob, &chaosKnob, &speedKnob, &pitchScope })
        addAndMakeVisible (c);

    // RAINBOW
    attachButton (rainbowPower, rbOn, "HIVE harmony section on/off (the VENOM footswitch engages it by itself)");
    attachButton (snapToggle, rbSnap, "Snap PITCH to semitones (off = atonal in-between intervals)");
    pitchKnob    .attach (state, rbPitch,     "Primary voice interval, -12..+12 semitones");
    primaryKnob  .attach (state, rbPrimary,   "DRONE: level of the main harmony voice");
    secondaryKnob.attach (state, rbSecondary, "QUEEN: a voice one octave from the DRONE (above for up-shifts, below for down)");
    toneKnob     .attach (state, rbTone,      "Brightness of the voices and of the regeneration loop");
    trackingKnob .attach (state, rbTracking,  "High = tight harmonies. Low = lag, long repeating grains and tone clusters");
    magicKnob    .attach (state, rbMagic,     "VENOM: regeneration - trails, resonance, cascading pitch spirals and self-oscillation");
    for (auto* c : std::initializer_list<juce::Component*> { &pitchKnob, &primaryKnob, &secondaryKnob, &toneKnob, &trackingKnob, &magicKnob })
        addAndMakeVisible (c);

    // SWARM
    attachButton (swarmPower, swarmOn,   "Swarm chorus on/off");
    attachButton (deepToggle, swarmDeep, "Deep mode: 8 voices with feedback");
    swarmDepthKnob.attach (state, swarmDepth, "Modulation depth");
    swarmRateKnob .attach (state, swarmRate,  "Modulation rate");
    swarmMixKnob  .attach (state, swarmMix,   "Chorus mix");
    for (auto* c : std::initializer_list<juce::Component*> { &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob })
        addAndMakeVisible (c);

    // FUZZ
    attachButton (fuzzPower, fuzzOn,   "SMOKE fuzz on/off");
    attachButton (postToggle, fuzzPost, "POST: fuzz after the pitch effects. Off: fuzz before them (glitchier tracking)");
    fuzzKnob    .attach (state, fuzz,     "Fuzz gain");
    fuzzToneKnob.attach (state, fuzzTone, "Dark <-> scooped <-> bright");
    fuzzGateKnob.attach (state, fuzzGate, "Starve the fuzz: sputtering, gated velcro decay");
    for (auto* c : std::initializer_list<juce::Component*> { &fuzzKnob, &fuzzToneKnob, &fuzzGateKnob })
        addAndMakeVisible (c);

    // FLOW
    attachButton (flowPower,  flowOn,   "WINGS gate on/off");
    attachButton (hardToggle, flowHard, "Hard stutter gate (off = smooth tremolo)");
    attachButton (syncToggle, flowSync, "Sync to host tempo");
    flowAmountKnob.attach (state, flowAmount, "Gate depth");
    flowSpeedKnob .attach (state, flowSpeed,  "Gate rate (free)");
    flowDivKnob   .attach (state, flowDiv,    "Gate rate (tempo division)");
    addAndMakeVisible (flowAmountKnob);
    addAndMakeVisible (flowSpeedKnob);
    addChildComponent (flowDivKnob);

    // OUTPUT
    mixKnob   .attach (state, mix,    "Dry / effect blend (latency aligned)");
    volumeKnob.attach (state, output, "Output level");
    addAndMakeVisible (mixKnob);
    addAndMakeVisible (volumeKnob);

    // Footswitches
    oct1Switch  .setTooltip ("+1 octave (hold, or click in LATCH mode) - works even while the plug-in is bypassed");
    oct2Switch  .setTooltip ("+2 octaves (hold, or click in LATCH mode) - works even while the plug-in is bypassed");
    magicSwitch .setTooltip ("VENOM: slams the regeneration into self-oscillation - works even with HIVE or the plug-in switched off. LINK switches bring the octaves along");
    bypassSwitch.setTooltip ("Plug-in on / bypass. The octave and VENOM footswitches still work while bypassed, like momentary pedals");
    attachButton (link1Switch, linkOct1, "LINK: pressing VENOM also engages +1 OCT");
    attachButton (link2Switch, linkOct2, "LINK: pressing VENOM also engages +2 OCT");
    for (auto* c : std::initializer_list<juce::Component*> { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch, &inMeter, &outMeter })
        addAndMakeVisible (c);

    addChildComponent (infoOverlay);

    setSize (baseWidth, baseHeight);
    tick();
}

void MainPanel::attachButton (juce::Button& b, const juce::String& id, const juce::String& tooltip)
{
    addAndMakeVisible (b);
    b.setTooltip (tooltip);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (state, id, b));
}

bool MainPanel::paramOn (const char* id) const
{
    return state.getRawParameterValue (id)->load() > 0.5f;
}

bool MainPanel::footswitchesMomentary() const
{
    return state.getRawParameterValue (ParamIDs::switchMode)->load() < 0.5f;
}

void MainPanel::setSectionDimmed (std::initializer_list<juce::Component*> comps, bool dimmed)
{
    for (auto* c : comps)
        c->setAlpha (dimmed ? 0.38f : 1.0f);
}

//==============================================================================
void MainPanel::resized()
{
    // Header
    presetBar.setBounds (300, 16, 400, 32);
    switchModeSelector.setBounds (736, 18, 196, 28);
    infoButton.setBounds (baseWidth - 16 - 32, 16, 32, 32);

    // Section areas
    noiseArea   = { 16.0f,  76.0f, 476.0f, 262.0f };
    rainbowArea = { 504.0f, 76.0f, 480.0f, 262.0f };
    const float rowY = 350.0f, rowH = 190.0f, w = 233.0f;
    swarmArea  = { 16.0f,  rowY, w, rowH };
    fuzzArea   = { 261.0f, rowY, w, rowH };
    flowArea   = { 506.0f, rowY, w, rowH };
    outputArea = { 751.0f, rowY, w, rowH };

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };
    auto pillFor  = [] (juce::Rectangle<float> a, int slot) { return juce::Rectangle<int> ((int) a.getRight() - 40 - 66 * (slot + 1), (int) a.getY() + 9, 60, 24); };

    // NOISE
    downToggle.setBounds ((int) noiseArea.getRight() - 16 - 70, (int) noiseArea.getY() + 9, 70, 24);
    {
        const int y = (int) noiseArea.getY() + 44;
        int x = (int) noiseArea.getX() + 22;
        for (auto* k : { &riseKnob, &panicKnob, &chaosKnob, &speedKnob })
        {
            k->setBounds (x, y, 100, 106);
            x += 112;
        }
        pitchScope.setBounds ((int) noiseArea.getX() + 16, (int) noiseArea.getY() + 160, (int) noiseArea.getWidth() - 32, 88);
    }

    // RAINBOW
    rainbowPower.setBounds (powerFor (rainbowArea));
    snapToggle.setBounds (pillFor (rainbowArea, 0));
    {
        const int x0 = (int) rainbowArea.getX() + 45;
        const int y1 = (int) rainbowArea.getY() + 40, y2 = (int) rainbowArea.getY() + 148;
        int i = 0;
        for (auto* k : { &pitchKnob, &primaryKnob, &secondaryKnob })
            k->setBounds (x0 + 145 * i++, y1, 100, 106);
        i = 0;
        for (auto* k : { &toneKnob, &trackingKnob, &magicKnob })
            k->setBounds (x0 + 145 * i++, y2, 100, 106);
    }

    auto threeKnobs = [] (juce::Rectangle<float> a, std::initializer_list<Knob*> knobs)
    {
        const int kw = 72, y = (int) a.getY() + 58;
        const int gap = ((int) a.getWidth() - kw * (int) knobs.size()) / ((int) knobs.size() + 1);
        int x = (int) a.getX() + gap;
        for (auto* k : knobs)
        {
            k->setBounds (x, y, kw, 104);
            x += kw + gap;
        }
    };

    // SWARM
    swarmPower.setBounds (powerFor (swarmArea));
    deepToggle.setBounds (pillFor (swarmArea, 0));
    threeKnobs (swarmArea, { &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob });

    // FUZZ
    fuzzPower.setBounds (powerFor (fuzzArea));
    postToggle.setBounds (pillFor (fuzzArea, 0));
    threeKnobs (fuzzArea, { &fuzzKnob, &fuzzToneKnob, &fuzzGateKnob });

    // FLOW
    flowPower.setBounds (powerFor (flowArea));
    syncToggle.setBounds ((int) flowArea.getRight() - 44 - 54,  (int) flowArea.getY() + 9, 54, 24);
    hardToggle.setBounds ((int) flowArea.getRight() - 44 - 112, (int) flowArea.getY() + 9, 54, 24);
    threeKnobs (flowArea, { &flowAmountKnob, &flowSpeedKnob });
    flowDivKnob.setBounds (flowSpeedKnob.getBounds());

    // OUTPUT
    threeKnobs (outputArea, { &mixKnob, &volumeKnob });

    // Footer: footswitches centred (LINK mini switches beside the octaves), meters at the sides
    {
        const int fy = 552, fw = 92, fh = 118, spacing = 136;
        const int total = spacing * 3 + fw;
        int x = (baseWidth - total) / 2;
        for (auto* f : { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch })
        {
            f->setBounds (x, fy, fw, fh);
            x += spacing;
        }
        link1Switch.setBounds (oct1Switch.getRight() + 2, fy + 34, 38, 50);
        link2Switch.setBounds (oct2Switch.getRight() + 2, fy + 34, 38, 50);
        inMeter .setBounds (16, 600, 200, 26);
        outMeter.setBounds (baseWidth - 16 - 200, 600, 200, 26);
    }

    infoOverlay.setBounds (getLocalBounds());
    backdrop.setBounds (getLocalBounds());
}

//==============================================================================
void MainPanel::paintBackdrop (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (Colours::backgroundHi, baseWidth * 0.5f, baseHeight * 0.35f,
                                             Colours::background, 0.0f, (float) baseHeight, true));
    g.fillAll();

    g.setColour (juce::Colours::white.withAlpha (0.012f));
    for (int i = -baseHeight; i < baseWidth; i += 6)
        g.drawLine ((float) i, 0.0f, (float) (i + baseHeight), (float) baseHeight, 1.0f);

    // Header
    {
        const auto header = juce::Rectangle<float> (0.0f, 0.0f, (float) baseWidth, 64.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1d22), 0.0f, 0.0f,
                                                 juce::Colour (0xff111215), 0.0f, header.getBottom(), false));
        g.fillRect (header);

        juce::ColourGradient line (Colours::accent.withAlpha (0.0f), 0.0f, 0.0f, Colours::accent.withAlpha (0.0f), (float) baseWidth, 0.0f, false);
        line.addColour (0.5, Colours::accent);
        g.setGradientFill (line);
        g.fillRect (0.0f, header.getBottom() - 1.5f, (float) baseWidth, 1.5f);

        if (logo.isValid())
        {
            const float h = 48.0f;
            const float lw = h * (float) logo.getWidth() / (float) logo.getHeight();
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, juce::Rectangle<float> (16.0f, 8.0f, lw, h), juce::RectanglePlacement::centred);

            g.setFont (font (13.0f, true));
            g.setColour (Colours::textDim);
            g.drawText ("STING / HIVE / SWARM", juce::Rectangle<float> (24.0f + lw, 22.0f, 190.0f, 20.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    for (auto a : { noiseArea, rainbowArea, swarmArea, fuzzArea, flowArea, outputArea })
        drawPanel (g, a);

    auto titleRow = [] (juce::Rectangle<float> a) { return a.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f); };

    drawSectionTitle (g, titleRow (noiseArea),   "STING",   lastSectionStates[0]);
    drawSectionTitle (g, titleRow (rainbowArea), "HIVE",    lastSectionStates[1]);
    drawSectionTitle (g, titleRow (swarmArea),   "SWARM",   lastSectionStates[2]);
    drawSectionTitle (g, titleRow (fuzzArea),    "SMOKE",   lastSectionStates[3]);
    drawSectionTitle (g, titleRow (flowArea),    "WINGS",   lastSectionStates[4]);
    drawSectionTitle (g, titleRow (outputArea),  "OUTPUT",  true);

    g.setFont (font (12.5f, true));
    g.setColour (Colours::textFaint);
    g.drawText ("hold the footswitches below", titleRow (noiseArea).withTrimmedLeft (80.0f),
                juce::Justification::centredLeft, false);

    g.setFont (font (13.0f));
    g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<float> (16.0f, (float) baseHeight - 30.0f, 120.0f, 20.0f),
                juce::Justification::centredLeft, false);
}

//==============================================================================
void MainPanel::tick()
{
    auto& meters = processor.getMeters();
    inMeter .update (meters.input[0].exchange (0.0f),  meters.input[1].exchange (0.0f));
    outMeter.update (meters.output[0].exchange (0.0f), meters.output[1].exchange (0.0f));

    const bool noiseOn = meters.noiseEngaged.load();

    // Octave LEDs also light when the VENOM footswitch drags them in through LINK.
    const bool venom = paramOn (ParamIDs::magicHold);
    oct1Switch.setLitExternally (venom && paramOn (ParamIDs::linkOct1));
    oct2Switch.setLitExternally (venom && paramOn (ParamIDs::linkOct2));
    pitchScope.push (meters.pitchSemitones.load(), noiseOn);

    presetBar.refresh();

    const bool synced = paramOn (ParamIDs::flowSync);
    flowSpeedKnob.setVisible (! synced);
    flowDivKnob.setVisible (synced);

    const std::array<bool, 5> states { noiseOn, paramOn (ParamIDs::rbOn), paramOn (ParamIDs::swarmOn),
                                       paramOn (ParamIDs::fuzzOn), paramOn (ParamIDs::flowOn) };

    setSectionDimmed ({ &snapToggle, &pitchKnob, &primaryKnob, &secondaryKnob, &toneKnob, &trackingKnob, &magicKnob }, ! states[1]);
    setSectionDimmed ({ &deepToggle, &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob }, ! states[2]);
    setSectionDimmed ({ &postToggle, &fuzzKnob, &fuzzToneKnob, &fuzzGateKnob }, ! states[3]);
    setSectionDimmed ({ &hardToggle, &syncToggle, &flowAmountKnob, &flowSpeedKnob, &flowDivKnob }, ! states[4]);

    if (states != lastSectionStates)
    {
        lastSectionStates = states;
        backdrop.repaint();
    }
}

//==============================================================================
SwarmnessAudioProcessorEditor::SwarmnessAudioProcessorEditor (SwarmnessAudioProcessor& p)
    : AudioProcessorEditor (&p), swarmProcessor (p), panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    const double ratio = (double) MainPanel::baseWidth / (double) MainPanel::baseHeight;
    setResizable (true, true);
    setResizeLimits ((int) (MainPanel::baseWidth * 0.7), (int) (MainPanel::baseHeight * 0.7),
                     MainPanel::baseWidth * 2, MainPanel::baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);

    const float scale = juce::jlimit (0.7f, 2.0f, swarmProcessor.getUiScale());
    setSize (juce::roundToInt (MainPanel::baseWidth * scale), juce::roundToInt (MainPanel::baseHeight * scale));

    startTimerHz (30);
}

SwarmnessAudioProcessorEditor::~SwarmnessAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SwarmnessAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void SwarmnessAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainPanel::baseWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setBounds (0, 0, MainPanel::baseWidth, MainPanel::baseHeight);
    swarmProcessor.setUiScale (scale);
}
