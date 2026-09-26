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
      fuzzVoiceSelector (param (state, ParamIDs::fuzzVoice), { "DOWN", "MID", "UP" }),
      oct1Switch   (param (state, ParamIDs::oct1),      "+1 OCT", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      oct2Switch   (param (state, ParamIDs::oct2),      "+2 OCT", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      magicSwitch  (param (state, ParamIDs::magicHold), "VENOM",  juce::Colour (0xffb46bff), false, [this] { return footswitchesMomentary(); }),
      bypassSwitch (param (state, ParamIDs::bypass),    "ON",     Colours::ledRed,  true,  nullptr)
{
    using namespace ParamIDs;

    logo = juce::ImageCache::getFromMemory (BinaryData::header_logo_png, BinaryData::header_logo_pngSize);
    emblem = juce::ImageCache::getFromMemory (BinaryData::emblem_png, BinaryData::emblem_pngSize);

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
    attachButton (stingRawToggle, stingRaw, "RAW: vintage lo-fi shifter like the pedal - grainy, buzzy octaves. Off = clean modern engine");
    riseKnob .attach (state, rise,  "RISE: time to glide into the octave when a footswitch goes down");
    fallKnob .attach (state, fall,  "FALL: time to glide back home when the footswitch is released");
    panicKnob.attach (state, panic, "ANGER: detunes the shifted signal against a second voice - dissonance, beating, sour clusters");
    chaosKnob.attach (state, chaos, "FRENZY: random pitch jumps around the octave - wider and faster as you turn it up");
    speedKnob.attach (state, speed, "BUZZ: all-pass feedback + amplitude modulation - slow phasing up to metallic ring-mod shrieks");
    stingMixKnob.attach (state, stingMix, "MIX: dry / shifted blend while a footswitch is down (100% = only the octave, like the pedal)");
    for (auto* c : std::initializer_list<juce::Component*> { &riseKnob, &fallKnob, &panicKnob, &chaosKnob, &speedKnob, &stingMixKnob, &pitchScope })
        addAndMakeVisible (c);

    // RAINBOW
    attachButton (rainbowPower, rbOn, "HIVE harmony section on/off (the VENOM footswitch engages it by itself)");
    attachButton (snapToggle, rbSnap, "Snap PITCH to semitones (off = atonal in-between intervals)");
    pitchKnob    .attach (state, rbPitch,     "PITCH: DRONE interval, -12..+12 semitones (SNAP = whole semitones). Click the value to type it");
    pitchKnob.setSnap ([this] (double v) { return paramOn (ParamIDs::rbSnap) ? std::round (v) : v; });
    primaryKnob  .attach (state, rbPrimary,   "DRONE: level of the main harmony voice");
    secondaryKnob.attach (state, rbSecondary, "QUEEN: a voice one octave from the DRONE (above for up-shifts, below for down)");
    toneKnob     .attach (state, rbTone,      "Brightness of the voices and of the regeneration loop");
    trackingKnob .attach (state, rbTracking,  "TRACKING: high = tight harmonies, low = lag, long repeating grains and tone clusters");
    magicKnob    .attach (state, rbMagic,     "TRAILS: repeats of the DRONE, each shifted by PITCH again - even ladders that fade out (the VENOM footswitch pushes it into self-oscillation)");
    rbTimeKnob   .attach (state, rbTime,      "TIME: time between the TRAILS repeats");
    rbMixKnob    .attach (state, rbMix,       "MIX: dry / HIVE voices. 50% = both at full level, 100% = only the HIVE voices and trails (the STING octave still sounds)");
    rbDivKnob    .attach (state, rbDiv,       "TIME as a tempo division (SYNC on)");
    attachButton (rbSyncToggle, rbSync, "SYNC: lock the TRAILS repeats to the host tempo");
    attachButton (rbRawToggle, rbRaw, "RAW: vintage FV-1-style shifter - warbly, dark, gritty voices; TRACKING sets its window. Off = clean modern engine");
    for (auto* c : std::initializer_list<juce::Component*> { &pitchKnob, &primaryKnob, &secondaryKnob, &toneKnob, &trackingKnob, &magicKnob, &rbTimeKnob, &rbMixKnob })
        addAndMakeVisible (c);
    addChildComponent (rbDivKnob);

    // SWARM
    attachButton (swarmPower, swarmOn,   "Swarm chorus on/off");
    attachButton (deepToggle, swarmDeep, "Deep mode: 8 voices with feedback");
    swarmDepthKnob.attach (state, swarmDepth, "Modulation depth");
    swarmRateKnob .attach (state, swarmRate,  "Modulation rate");
    swarmMixKnob  .attach (state, swarmMix,   "Chorus mix: 50% = dry and chorus both at full level, 100% = pure vibrato");
    for (auto* c : std::initializer_list<juce::Component*> { &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob })
        addAndMakeVisible (c);

    // FUZZ
    attachButton (fuzzPower, fuzzOn,   "SMOKE fuzz on/off");
    attachButton (postToggle, fuzzPost, "POST: fuzz after the pitch effects. Off: fuzz before them (glitchier tracking)");
    fuzzVoiceSelector.setTooltip ("VOICE: DOWN = doom low-mids and full bottom, MID = jumbo fuzz, UP = tight, screaming upper mids");
    fuzzKnob     .attach (state, fuzz,      "FUZZ: from dirty crunch to wall-of-fuzz sustain");
    fuzzToneKnob .attach (state, fuzzTone,  "TONE: dark <-> bright (also opens the fizz)");
    fuzzScoopKnob.attach (state, fuzzScoop, "SCOOP: mid cut depth - flat mids at 0, deep jumbo-fuzz scoop at max");
    fuzzGlareKnob.attach (state, fuzzGlare, "GLARE: gated octave-up that rips through on hard picking");
    fuzzGateKnob .attach (state, fuzzGate,  "GATE: starve the fuzz - sputtering, gated velcro decay");
    fuzzBlendKnob.attach (state, fuzzBlend, "BLEND: clean signal under the fuzz (pick attack and low end)");
    for (auto* c : std::initializer_list<juce::Component*> { &fuzzVoiceSelector, &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob,
                                                             &fuzzGlareKnob, &fuzzGateKnob, &fuzzBlendKnob })
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
    inputKnob .attach (state, input,  "INPUT sensitivity: how hard the effects are hit (SMOKE, tracking). Aim for peaks in the green zone of the IN meter; the output level is compensated");
    addAndMakeVisible (inputKnob);
    inMeter.setTargetZone (-18.0f, -6.0f);
    inMeter.setTooltip ("Input level after INPUT - aim for peaks in the green zone");
    volumeKnob.attach (state, output, "Output level");
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
    presetBar.setBounds (222, 16, 580, 32);
    switchModeSelector.setBounds (822, 18, 196, 28);
    infoButton.setBounds (baseWidth - 16 - 32, 16, 32, 32);

    // Section areas
    noiseArea   = { 16.0f,  76.0f, 536.0f, 262.0f };
    rainbowArea = { 564.0f, 76.0f, 520.0f, 262.0f };
    const float rowY = 350.0f, rowH = 190.0f;
    swarmArea  = { 16.0f,  rowY, 250.0f, rowH };
    fuzzArea   = { 278.0f, rowY, 500.0f, rowH };
    flowArea   = { 790.0f, rowY, 294.0f, rowH };

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };
    auto pillFor  = [] (juce::Rectangle<float> a, int slot) { return juce::Rectangle<int> ((int) a.getRight() - 40 - 66 * (slot + 1), (int) a.getY() + 9, 60, 24); };

    // NOISE
    downToggle.setBounds ((int) noiseArea.getRight() - 16 - 70, (int) noiseArea.getY() + 9, 70, 24);
    stingRawToggle.setBounds (downToggle.getX() - 6 - 64, (int) noiseArea.getY() + 9, 64, 24);
    {
        const int y = (int) noiseArea.getY() + 44;
        int x = (int) noiseArea.getX() + 12;
        for (auto* k : { &riseKnob, &fallKnob, &panicKnob, &chaosKnob, &speedKnob, &stingMixKnob })
        {
            k->setBounds (x, y, 84, 106);
            x += 85;
        }
        pitchScope.setBounds ((int) noiseArea.getX() + 16, (int) noiseArea.getY() + 160, (int) noiseArea.getWidth() - 32, 88);
    }

    // RAINBOW
    rainbowPower.setBounds (powerFor (rainbowArea));
    snapToggle.setBounds (pillFor (rainbowArea, 0));
    rbSyncToggle.setBounds (pillFor (rainbowArea, 1));
    rbRawToggle.setBounds (pillFor (rainbowArea, 2));
    {
        const int y1 = (int) rainbowArea.getY() + 40, y2 = (int) rainbowArea.getY() + 148;
        int x0 = (int) rainbowArea.getX() + 20, i = 0;
        for (auto* k : { &pitchKnob, &primaryKnob, &secondaryKnob, &rbMixKnob })
            k->setBounds (x0 + 122 * i++, y1, 100, 106);
        i = 0;
        for (auto* k : { &toneKnob, &trackingKnob, &magicKnob, &rbTimeKnob })
            k->setBounds (x0 + 122 * i++, y2, 100, 106);
        rbDivKnob.setBounds (rbTimeKnob.getBounds());
    }

    auto threeKnobs = [] (juce::Rectangle<float> a, std::initializer_list<Knob*> knobs)
    {
        const int kw = knobs.size() > 3 ? 70 : 72, y = (int) a.getY() + 58;
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
    fuzzVoiceSelector.setBounds (postToggle.getX() - 8 - 150, (int) fuzzArea.getY() + 10, 150, 22);
    threeKnobs (fuzzArea, { &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob, &fuzzGlareKnob, &fuzzGateKnob, &fuzzBlendKnob });

    // FLOW
    flowPower.setBounds (powerFor (flowArea));
    syncToggle.setBounds ((int) flowArea.getRight() - 44 - 54,  (int) flowArea.getY() + 9, 54, 24);
    hardToggle.setBounds ((int) flowArea.getRight() - 44 - 112, (int) flowArea.getY() + 9, 54, 24);
    threeKnobs (flowArea, { &flowAmountKnob, &flowSpeedKnob });
    flowDivKnob.setBounds (flowSpeedKnob.getBounds());


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
        footswitchArea = juce::Rectangle<float> ((float) oct1Switch.getX() - 14.0f, (float) fy - 4.0f,
                                                 (float) (bypassSwitch.getRight() - oct1Switch.getX()) + 28.0f, (float) fh + 6.0f);
        inputKnob .setBounds (16, fy - 8, 72, 104);
        inMeter   .setBounds (96, 600, 176, 26);
        volumeKnob.setBounds (baseWidth - 16 - 72, fy - 8, 72, 104);
        outMeter  .setBounds (baseWidth - 16 - 72 - 8 - 176, 600, 176, 26);
    }

    infoOverlay.setBounds (getLocalBounds());
    backdrop.setBounds (getLocalBounds());
}

//==============================================================================
void MainPanel::paintBackdrop (juce::Graphics& g)
{
    const auto all = juce::Rectangle<float> ((float) baseWidth, (float) baseHeight);
    g.setGradientFill (juce::ColourGradient (Colours::backgroundHi, baseWidth * 0.5f, baseHeight * 0.45f,
                                             Colours::background, 0.0f, (float) baseHeight, true));
    g.fillAll();

    // Honeycomb wall
    drawHoneycomb (g, all, 30.0f, Colours::accent.withAlpha (0.045f), 1.4f);

    // The necro-bee, looming behind everything
    if (emblem.isValid())
    {
        const float h = 640.0f, w = h * (float) emblem.getWidth() / (float) emblem.getHeight();
        g.setOpacity (0.2f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (emblem, juce::Rectangle<float> (w, h).withCentre ({ baseWidth * 0.5f, baseHeight * 0.52f }),
                     juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    drawGrime (g, all, 0.6f);

    // Vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, baseWidth * 0.5f, baseHeight * 0.5f,
                                             juce::Colours::black.withAlpha (0.75f), 0.0f, 0.0f, true));
    g.fillAll();

    // Header
    {
        const auto header = juce::Rectangle<float> (0.0f, 0.0f, (float) baseWidth, 64.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xf01a130d), 0.0f, 0.0f,
                                                 juce::Colour (0xf00b0806), 0.0f, header.getBottom(), false));
        g.fillRect (header);
        drawHoneycomb (g, header, 9.0f, Colours::accent.withAlpha (0.05f), 0.8f);

        // Honey line with drips
        juce::ColourGradient line (Colours::accentDeep.withAlpha (0.0f), 0.0f, 0.0f, Colours::accentDeep.withAlpha (0.0f), (float) baseWidth, 0.0f, false);
        line.addColour (0.2, Colours::accent);
        line.addColour (0.5, Colours::accentBright);
        line.addColour (0.8, Colours::accent);
        g.setGradientFill (line);
        g.fillRect (0.0f, header.getBottom() - 2.0f, (float) baseWidth, 2.0f);

        juce::Random rng (1337);
        for (int i = 0; i < 11; ++i)
        {
            const float x = 180.0f + rng.nextFloat() * ((float) baseWidth - 240.0f);
            const float len = 3.0f + std::pow (rng.nextFloat(), 2.0f) * 16.0f;
            const float w = 1.5f + rng.nextFloat() * 2.0f;
            const float top = header.getBottom() - 1.0f;
            juce::Path drip;
            drip.startNewSubPath (x - w, top);
            drip.quadraticTo (x - w * 0.4f, top + len * 0.6f, x - w * 0.5f, top + len);
            drip.addCentredArc (x, top + len, w * 0.5f, w * 0.6f, 0.0f, -juce::MathConstants<float>::halfPi,
                                juce::MathConstants<float>::halfPi, false);
            drip.quadraticTo (x + w * 0.4f, top + len * 0.6f, x + w, top);
            drip.closeSubPath();
            const float t = x / (float) baseWidth;
            g.setColour ((t > 0.3f && t < 0.7f ? Colours::accentBright : Colours::accent).withAlpha (0.35f + 0.5f * (1.0f - std::abs (t - 0.5f) * 2.0f)));
            g.fillPath (drip);
        }

        if (logo.isValid())
        {
            const float h = 70.0f;   // breaks out of the header a little, like the pedal's artwork
            const float lw = h * (float) logo.getWidth() / (float) logo.getHeight();
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, juce::Rectangle<float> (10.0f, 1.0f, lw, h), juce::RectanglePlacement::centred);
        }
    }

    for (auto a : { noiseArea, rainbowArea, swarmArea, fuzzArea, flowArea })
        drawPanel (g, a);

    // Footswitch plate (pedalboard strip behind the stomps and LINK switches)
    drawPanel (g, footswitchArea, 12.0f);

    auto titleRow = [] (juce::Rectangle<float> a) { return a.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f); };

    drawSectionTitle (g, titleRow (noiseArea),   "STING",   lastSectionStates[0]);
    drawSectionTitle (g, titleRow (rainbowArea), "HIVE",    lastSectionStates[1]);
    drawSectionTitle (g, titleRow (swarmArea),   "SWARM",   lastSectionStates[2]);
    drawSectionTitle (g, titleRow (fuzzArea),    "SMOKE",   lastSectionStates[3]);
    drawSectionTitle (g, titleRow (flowArea),    "WINGS",   lastSectionStates[4]);

    g.setFont (font (12.5f, true));
    g.setColour (Colours::textFaint);
    g.drawText ("hold the footswitches below", titleRow (noiseArea).withTrimmedLeft (98.0f),
                juce::Justification::centredLeft, false);

    g.setFont (font (13.0f));
    g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<float> (16.0f, (float) baseHeight - 22.0f, 120.0f, 16.0f),
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
    const bool hiveSynced = paramOn (ParamIDs::rbSync);
    rbTimeKnob.setVisible (! hiveSynced);
    rbDivKnob.setVisible (hiveSynced);

    const std::array<bool, 5> states { noiseOn, paramOn (ParamIDs::rbOn), paramOn (ParamIDs::swarmOn),
                                       paramOn (ParamIDs::fuzzOn), paramOn (ParamIDs::flowOn) };

    setSectionDimmed ({ &snapToggle, &rbSyncToggle, &rbRawToggle, &pitchKnob, &primaryKnob, &secondaryKnob, &toneKnob, &trackingKnob,
                        &magicKnob, &rbTimeKnob, &rbDivKnob, &rbMixKnob }, ! states[1]);
    setSectionDimmed ({ &deepToggle, &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob }, ! states[2]);
    setSectionDimmed ({ &postToggle, &fuzzVoiceSelector, &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob,
                        &fuzzGlareKnob, &fuzzGateKnob, &fuzzBlendKnob }, ! states[3]);
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
