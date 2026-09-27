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
      chainStrip (p.getAPVTS()),
      eqPage (p),
      reverbPage (p),
      presetBar (p.getPresetManager()),
      switchModeSelector (param (state, ParamIDs::switchMode), { "MOMENTARY", "LATCH" }),
      stepGrid (p.getAPVTS()),
      fuzzVoiceSelector (param (state, ParamIDs::fuzzVoice), { "DOWN", "MID", "UP" }),
      oct1Switch   (param (state, ParamIDs::oct1),      "SHIFT A", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      oct2Switch   (param (state, ParamIDs::oct2),      "SHIFT B", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
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

    // Pages
    fxPage.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (fxPage);
    addChildComponent (eqPage);
    addChildComponent (reverbPage);

    // Chain strip
    chainStrip.getLayout = [this] { return processor.getRequestedLayout(); };
    chainStrip.setLayout = [this] (const Chain::Layout& l) { processor.setChainLayout (l); };
    chainStrip.onBlockClicked = [this] (int block)
    {
        if (mini)
            setMini (false);   // a tile opens the full view at its page
        showPage (pageForBlock (block));
    };
    chainStrip.onBlockRightClick = [this] (int block)
    {
        if (auto* id = ChainStrip::powerParamFor (block))
            showMidiMenu (id, &chainStrip);
    };
    chainStrip.isBlockActive = [this] (int block)
    {
        return (block == Chain::shift && processor.getMeters().noiseEngaged.load())
            || (block == Chain::pitch && paramOn (ParamIDs::magicHold));
    };
    addAndMakeVisible (chainStrip);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (switchModeSelector);
    switchModeSelector.setTooltip ("Footswitches: MOMENTARY = active only while held, LATCH = click on / click off");
    addAndMakeVisible (infoButton);
    infoButton.setTooltip ("Help");
    infoButton.onClick = [this] { infoOverlay.setVisible (true); infoOverlay.toFront (false); };
    addAndMakeVisible (miniButton);
    miniButton.setTooltip ("MINI: a small window with just the chain, the scenes and the footswitches - for playing live. Click again for the full view");
    miniButton.onClick = [this] { setMini (! mini); };

    // Scenes
    auto& pm = processor.getPresetManager();
    sceneBar.getCurrent = [&pm] { return pm.getCurrentScene(); };
    sceneBar.isUsed = [&pm] (int k) { return pm.isSceneUsed (k); };
    sceneBar.describeMidi = [this] (int k) { return processor.describeMidiBinding (ParamIDs::scene, k); };
    sceneBar.onSelect = [this] (int k)
    {
        if (auto* p = state.getParameter (ParamIDs::scene))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) k));
            p->endChangeGesture();
        }
    };
    sceneBar.onRightClick = [this] (int k) { showMidiMenu (ParamIDs::scene, &sceneBar, k); };
    addAndMakeVisible (sceneBar);

    // Pages
    pitchPage.setInterceptsMouseClicks (false, true);
    addChildComponent (pitchPage);

    // SHIFT (footswitch shifter: the power button keeps SHIFT A on, the footswitches engage it while held)
    attachButton (pitchPage, shiftPower, shOn, "SHIFT on / off: on = SHIFT A sounds all the time (like a latched pedal). "
                                               "The SHIFT A / B footswitches still engage it while held when it is off"); 
    shiftAKnob.attach (state, shiftA, "SHIFT A: interval of the SHIFT A footswitch, -24..+24 semitones (octaves, fifths, fourths...)");
    shiftBKnob.attach (state, shiftB, "SHIFT B: interval of the SHIFT B footswitch (while both are held it wins - or both sound with STACK)");
    riseKnob  .attach (state, rise,   "RISE: time to glide into the interval when a SHIFT footswitch goes down");
    fallKnob  .attach (state, fall,   "FALL: time to glide back home when the footswitch is released");
    blendKnob .attach (state, stingMix, "MIX: dry / shifted while SHIFT is engaged. 50% = both at full level (doubled), 100% = only the shifted note");
    panicKnob .attach (state, panic,  "ANGER (The Noise: Panic): a second voice detuned against the shifted note - sour beating, dissonant clusters");
    chaosKnob .attach (state, chaos,  "FRENZY (The Noise: Chaos): random pitch jumps, wider and faster as you turn it up (SNAP = on 4ths / 5ths / octaves)");
    speedKnob .attach (state, speed,  "BUZZ (The Noise: Speed): all-pass feedback + amplitude modulation - slow phasing up to ring-mod shrieks");
    shDetuneKnob.attach (state, shDetune, "DETUNE: fine offset of the shifted note, -50..+50 cents - a sour SHIFT");
    attachButton (pitchPage, stackToggle, shStack, "STACK: holding SHIFT A and B together plays both intervals at once (a second voice splits off to B). Off = B wins");
    attachButton (pitchPage, shSnapToggle, shSnap, "SNAP: FRENZY jumps land on 4ths / 5ths / octaves (off = random in-between pitches)");
    attachButton (pitchPage, shRawToggle, shRaw, "RAW: cheap-pedal-DSP character - warble, rough splices, lo-fi converters. Off = clean modern engine");
    pitchScope.setTooltip ("Live SHIFT transposition (with FRENZY / ANGER movement)");

    // HIVE - VOICES
    attachButton (pitchPage, hivePower, rbOn, "HIVE on/off (voices + trails). The VENOM footswitch switches it on while held, like a momentary pedal");
    attachButton (pitchPage, snapToggle, rbSnap, "SNAP: PITCH in whole semitones, and FRENZY jumps land on 4ths / 5ths / octaves (off = atonal in-between pitches)");
    pitchKnob    .attach (state, rbPitch,     "PITCH: DRONE interval, -12..+12 semitones (SNAP = whole semitones). Also how far an UP / DOWN step moves a repeat");
    pitchKnob.setSnap ([this] (double v) { return paramOn (ParamIDs::rbSnap) ? std::round (v) : v; });
    primaryKnob  .attach (state, rbPrimary,   "DRONE: level of the main harmony voice (and its trails). HIVE harmonises what reaches it: after SHIFT in the chain it follows the shifted note");
    secondaryKnob.attach (state, rbSecondary, "QUEEN: a voice one octave from the DRONE (above for up-shifts, below for down)");
    trackingKnob .attach (state, rbTracking,  "TRACKING: high = tight harmonies, low = lag, long repeating grains and tone clusters");
    // TRAILS
    magicKnob .attach (state, rbMagic, "TRAILS: how long the repeats of the DRONE keep going - the STEPS below shape each one (the VENOM footswitch pushes them into self-oscillation)");
    rbTimeKnob.attach (state, rbTime,  "TIME: time between the repeats = length of one step");
    rbDivKnob .attach (state, rbDiv,   "TIME as a tempo division (SYNC on) = length of one step");
    toneKnob  .attach (state, rbTone,  "TONE: brightness of the voices and the trails");
    gateKnob  .attach (state, trChop,  "GATE: chops every step - 0% = whole repeats, higher = shorter, stuttering chops");
    attachButton (pitchPage, trDryToggle, trDry, "DRY: the repeats start from your note instead of the DRONE - HIVE becomes a delay "
                                                 "(HOLD steps = plain echoes, UP / DOWN = a pitch-shifting delay). DRONE / QUEEN still sound on top if turned up");
    attachButton (pitchPage, rbSyncToggle, rbSync, "SYNC: lock the repeats to the host tempo - while the song plays, the STEPS follow the bar grid");
    // MANGLE
    hvMangleKnob.attach (state, hvMangle, "MANGLE: chaos for the voices and trails in one knob - first sour detuned voices (ANGER), "
                                          "then random pitch jumps (FRENZY, on 4ths / 5ths / octaves with SNAP), then all-pass buzz and AM (BUZZ) on top");
    rbDetuneKnob.attach (state, rbDetune, "DETUNE: -50..+50 cents - DRONE up / QUEEN down for width");
    rbMixKnob.attach (state, rbMix, "MIX: dry / HIVE voices. 50% = both at full level, 100% = only the voices and trails");
    attachButton (pitchPage, rbRawToggle, rbRaw, "RAW: cheap-pedal-DSP character for the voices - warble, rough splices, lo-fi converters. Off = clean modern engine");

    for (auto* c : std::initializer_list<juce::Component*> { &pitchScope, &shiftAKnob, &shiftBKnob, &riseKnob, &fallKnob, &blendKnob,
                                                             &panicKnob, &chaosKnob, &speedKnob, &shDetuneKnob,
                                                             &pitchKnob, &primaryKnob, &secondaryKnob, &trackingKnob,
                                                             &magicKnob, &rbTimeKnob, &toneKnob, &gateKnob, &stepGrid,
                                                             &hvMangleKnob, &rbDetuneKnob, &rbMixKnob })
        pitchPage.addAndMakeVisible (c);
    pitchPage.addChildComponent (rbDivKnob);

    // SWARM
    attachButton (fxPage, swarmPower, swarmOn,   "Swarm chorus on/off");
    attachButton (fxPage, deepToggle, swarmDeep, "Deep mode: 8 voices with feedback");
    swarmDepthKnob.attach (state, swarmDepth, "Modulation depth");
    swarmRateKnob .attach (state, swarmRate,  "Modulation rate");
    swarmMixKnob  .attach (state, swarmMix,   "Chorus mix: 50% = dry and chorus both at full level, 100% = pure vibrato");
    for (auto* c : std::initializer_list<juce::Component*> { &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob })
        fxPage.addAndMakeVisible (c);

    // FUZZ
    attachButton (fxPage, fuzzPower, fuzzOn,   "SMOKE fuzz on/off");
    fuzzVoiceSelector.setTooltip ("VOICE: DOWN = doom low-mids and full bottom, MID = jumbo fuzz, UP = tight, screaming upper mids");
    fuzzKnob     .attach (state, fuzz,      "FUZZ: from dirty crunch to wall-of-fuzz sustain");
    fuzzToneKnob .attach (state, fuzzTone,  "TONE: dark <-> bright (also opens the fizz)");
    fuzzScoopKnob.attach (state, fuzzScoop, "SCOOP: mid cut depth - flat mids at 0, deep jumbo-fuzz scoop at max");
    fuzzGlareKnob.attach (state, fuzzGlare, "GLARE: gated octave-up that rips through on hard picking");
    fuzzGateKnob .attach (state, fuzzGate,  "GATE: starve the fuzz - sputtering, gated velcro decay");
    fuzzBlendKnob.attach (state, fuzzBlend, "CLEAN: clean signal added under the full fuzz (pick attack and low end)");
    fuzzSagKnob  .attach (state, fuzzSag,   "SAG: how much the fuzz breathes - the supply sags on the pick (compressed, darker) and the note blooms back as it recovers");
    for (auto* c : std::initializer_list<juce::Component*> { &fuzzVoiceSelector, &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob,
                                                             &fuzzGlareKnob, &fuzzGateKnob, &fuzzSagKnob, &fuzzBlendKnob })
        fxPage.addAndMakeVisible (c);

    // FLOW
    attachButton (fxPage, flowPower,  flowOn,   "WINGS gate on/off");
    attachButton (fxPage, hardToggle, flowHard, "Hard stutter gate (off = smooth tremolo)");
    attachButton (fxPage, syncToggle, flowSync, "Sync to host tempo");
    flowAmountKnob.attach (state, flowAmount, "Gate depth");
    flowSpeedKnob .attach (state, flowSpeed,  "Gate rate (free)");
    flowDivKnob   .attach (state, flowDiv,    "Gate rate (tempo division)");
    fxPage.addAndMakeVisible (flowAmountKnob);
    fxPage.addAndMakeVisible (flowSpeedKnob);
    fxPage.addChildComponent (flowDivKnob);

    // OUTPUT
    inputKnob .attach (state, input,  "INPUT sensitivity: how hard the effects are hit (SMOKE, tracking). Aim for peaks in the green zone of the IN meter; the output level is compensated");
    addAndMakeVisible (inputKnob);
    inMeter.setTargetZone (-18.0f, -6.0f);
    inMeter.setTooltip ("Input level after INPUT - aim for peaks in the green zone");
    volumeKnob.attach (state, output, "Output level");
    addAndMakeVisible (volumeKnob);

    // Footswitches
    oct1Switch  .setTooltip ("SHIFT A: transposes by the SHIFT A interval (hold, or click in LATCH mode) - works even while the plug-in is bypassed. Right-click: MIDI learn");
    oct2Switch  .setTooltip ("SHIFT B: transposes by the SHIFT B interval (wins over A, or adds to it with STACK) - works even while the plug-in is bypassed. Right-click: MIDI learn");
    magicSwitch .setTooltip ("VENOM: switches HIVE on and slams its trails into self-oscillation while held - works even with HIVE or the plug-in off. LINK switches bring SHIFT A / B along. Right-click: MIDI learn");
    bypassSwitch.setTooltip ("Plug-in on / bypass. The octave and VENOM footswitches still work while bypassed, like momentary pedals. Right-click: MIDI learn");
    attachButton (*this, link1Switch, linkOct1, "LINK: pressing VENOM also engages SHIFT A");
    attachButton (*this, link2Switch, linkOct2, "LINK: pressing VENOM also engages SHIFT B");
    for (auto* c : std::initializer_list<juce::Component*> { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch, &inMeter, &outMeter })
        addAndMakeVisible (c);

    learnMarker.setInterceptsMouseClicks (false, false);
    addChildComponent (learnMarker);
    addMouseListener (this, true);   // right-clicks anywhere -> MIDI menu of the control under the mouse

    addChildComponent (infoOverlay);

    mini = processor.getUiMini();
    setSize (baseWidth, getBaseHeight());
    showPage (processor.getUiPage());
    tick();
}

void MainPanel::attachButton (juce::Component& parent, juce::Button& b, const juce::String& id, const juce::String& tooltip)
{
    parent.addAndMakeVisible (b);
    b.setTooltip (tooltip);
    MidiLearnable::tag (b, id);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (state, id, b));
}

int MainPanel::pageForBlock (int block)
{
    switch (block)
    {
        case Chain::comb:
        case Chain::carve: return eqPageIndex;
        case Chain::crypt: return spacePageIndex;
        case Chain::shift:
        case Chain::pitch: return pitchPageIndex;
        default:           return fxPageIndex;
    }
}

void MainPanel::setMini (bool shouldBeMini)
{
    if (shouldBeMini == mini)
        return;
    mini = shouldBeMini;
    processor.setUiMini (mini);
    setSize (baseWidth, getBaseHeight());
    showPage (currentPage);
    if (onModeChanged != nullptr)
        onModeChanged();
}

void MainPanel::showPage (int page)
{
    currentPage = juce::jlimit (0, numPages - 1, page);
    processor.setUiPage (currentPage);
    fxPage.setVisible (! mini && currentPage == fxPageIndex);
    pitchPage.setVisible (! mini && currentPage == pitchPageIndex);
    eqPage.setVisible (! mini && currentPage == eqPageIndex);
    reverbPage.setVisible (! mini && currentPage == spacePageIndex);
    miniButton.setToggleState (mini, juce::dontSendNotification);
    miniButton.setButtonText (mini ? "FULL" : "MINI");
    if (currentPage != eqPageIndex)
        processor.getSpectrumTap().setActive (false);

    std::array<bool, Chain::numBlocks> hi {};
    for (int b = 0; b < Chain::numBlocks; ++b)
        hi[(size_t) b] = pageForBlock (b) == currentPage;
    chainStrip.setHighlighted (hi);
    backdrop.repaint();
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
    switchModeSelector.setBounds (818, 18, 170, 28);
    miniButton.setBounds (baseWidth - 16 - 32 - 8 - 52, 16, 52, 32);
    infoButton.setBounds (baseWidth - 16 - 32, 16, 32, 32);

    // Chain strip and the page area below it
    chainStrip.setBounds (16, 70, baseWidth - 32, 66);
    const auto pageArea = juce::Rectangle<int> (16, 144, baseWidth - 32, 464);
    eqPage.setBounds (pageArea);
    reverbPage.setBounds (pageArea);

    fxPage.setBounds (getLocalBounds());
    pitchPage.setBounds (getLocalBounds());

    // FX page: SMOKE, SWARM, WINGS side by side (room below for more blocks)
    fuzzArea   = { 16.0f,  144.0f, 540.0f, 200.0f };
    swarmArea  = { 568.0f, 144.0f, 246.0f, 200.0f };
    flowArea   = { 826.0f, 144.0f, (float) baseWidth - 16.0f - 826.0f, 200.0f };
    // PITCH page: HIVE on top, SHIFT below
    hiveArea   = { 16.0f, 144.0f, (float) baseWidth - 32.0f, 262.0f };
    shiftArea  = { 16.0f, 418.0f, (float) baseWidth - 32.0f, 190.0f };

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };
    auto pillFor  = [] (juce::Rectangle<float> a, int slot) { return juce::Rectangle<int> ((int) a.getRight() - 40 - 66 * (slot + 1), (int) a.getY() + 9, 60, 24); };

    // HIVE: VOICES | TRAILS | MANGLE
    {
        const float widths[3] { 262.0f, 500.0f, 0.0f };
        float x = hiveArea.getX();
        for (int i = 0; i < 3; ++i)
        {
            const float w = i < 2 ? widths[i] : hiveArea.getRight() - x;
            hiveSections[(size_t) i] = { x, hiveArea.getY(), w, hiveArea.getHeight() };
            x += w;
        }
        const int y1 = (int) hiveArea.getY() + 58, y2 = (int) hiveArea.getY() + 158, kh = 100;
        int kw = 72;
        auto row = [&] (juce::Rectangle<float> sec, int y, std::initializer_list<juce::Component*> comps, int slots = 3)
        {
            const int gap = ((int) sec.getWidth() - kw * slots) / (slots + 1);
            int cx = (int) sec.getX() + gap;
            for (auto* c : comps)
            {
                if (c != nullptr)
                    c->setBounds (cx, y, kw, kh);
                cx += kw + gap;
            }
        };
        const auto& voiceSec = hiveSections[0];
        const auto& trailSec = hiveSections[1];
        const auto& mangleSec = hiveSections[2];

        hivePower.setBounds (powerFor (hiveArea));

        row (voiceSec, y1, { &pitchKnob, &primaryKnob, &secondaryKnob });
        row (voiceSec, y2, { &trackingKnob, nullptr, nullptr });
        // switches live in the section's heading row, right-aligned
        snapToggle.setBounds ((int) voiceSec.getRight() - 12 - 60, (int) voiceSec.getY() + 36, 60, 22);

        rbSyncToggle.setBounds ((int) trailSec.getRight() - 12 - 60, (int) trailSec.getY() + 36, 60, 22);
        trDryToggle .setBounds ((int) trailSec.getRight() - 12 - 126, (int) trailSec.getY() + 36, 60, 22);
        // TRAILS: four small knobs (2 x 2) on the left, a tall step grid on the right (easy to draw)
        {
            const int kx = (int) trailSec.getX() + 12, ky = (int) hiveArea.getY() + 62, sw = 66, sh = 96;
            magicKnob .setBounds (kx,      ky,      sw, sh);
            rbTimeKnob.setBounds (kx + sw, ky,      sw, sh);
            toneKnob  .setBounds (kx,      ky + sh, sw, sh);
            gateKnob  .setBounds (kx + sw, ky + sh, sw, sh);
            rbDivKnob.setBounds (rbTimeKnob.getBounds());
            const int gx = kx + 2 * sw + 12;
            stepGrid.setBounds (gx, (int) hiveArea.getY() + 64, (int) trailSec.getRight() - 14 - gx, (int) hiveArea.getBottom() - 12 - ((int) hiveArea.getY() + 64));
        }

        rbRawToggle.setBounds ((int) mangleSec.getRight() - 12 - 60, (int) mangleSec.getY() + 36, 60, 22);
        row (mangleSec, y1, { &hvMangleKnob }, 1);
        row (mangleSec, y2, { &rbDetuneKnob, &rbMixKnob }, 2);
    }

    // SHIFT: live pitch display on the left, nine knobs, STACK / SNAP / RAW in the title row
    {
        pitchScope.setBounds ((int) shiftArea.getX() + 16, (int) shiftArea.getY() + 44, 250, 132);
        shiftPower  .setBounds (powerFor (shiftArea));
        stackToggle .setBounds (pillFor (shiftArea, 2));
        shSnapToggle.setBounds (pillFor (shiftArea, 1));
        shRawToggle .setBounds (pillFor (shiftArea, 0));
        const int x0 = (int) shiftArea.getX() + 282, x1 = (int) shiftArea.getRight() - 10, kw = 72;
        const std::initializer_list<Knob*> knobs { &shiftAKnob, &shiftBKnob, &riseKnob, &fallKnob, &blendKnob,
                                                   &panicKnob, &chaosKnob, &speedKnob, &shDetuneKnob };
        const float step = (float) (x1 - x0) / (float) knobs.size();
        int k = 0;
        for (auto* knob : knobs)
            knob->setBounds (x0 + juce::roundToInt (step * ((float) k++ + 0.5f)) - kw / 2, (int) shiftArea.getY() + 52, kw, 104);
    }

    auto threeKnobs = [] (juce::Rectangle<float> a, std::initializer_list<Knob*> knobs)
    {
        const int kw = knobs.size() > 6 ? 64 : (knobs.size() > 3 ? 70 : 72), y = (int) a.getY() + 58;
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
    fuzzVoiceSelector.setBounds ((int) fuzzArea.getRight() - 40 - 8 - 150, (int) fuzzArea.getY() + 10, 150, 22);
    threeKnobs (fuzzArea, { &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob, &fuzzGlareKnob, &fuzzGateKnob, &fuzzSagKnob, &fuzzBlendKnob });

    // FLOW
    flowPower.setBounds (powerFor (flowArea));
    syncToggle.setBounds ((int) flowArea.getRight() - 44 - 54,  (int) flowArea.getY() + 9, 54, 24);
    hardToggle.setBounds ((int) flowArea.getRight() - 44 - 112, (int) flowArea.getY() + 9, 54, 24);
    threeKnobs (flowArea, { &flowAmountKnob, &flowSpeedKnob });
    flowDivKnob.setBounds (flowSpeedKnob.getBounds());


    // Footer: footswitches centred (LINK mini switches beside the octaves), meters at the sides
    {
        // scenes above the footswitches (full view: under the pages)
        sceneBar.setBounds (16, mini ? 144 : 616, baseWidth - 32, 40);
        const int fy = mini ? 196 : 668, fw = 92, fh = 118, spacing = 136;
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
        inMeter   .setBounds (96, fy + 48, 176, 26);
        volumeKnob.setBounds (baseWidth - 16 - 72, fy - 8, 72, 104);
        outMeter  .setBounds (baseWidth - 16 - 72 - 8 - 176, fy + 48, 176, 26);
    }

    infoOverlay.setBounds (getLocalBounds());
    backdrop.setBounds (getLocalBounds());
}

//==============================================================================
void MainPanel::paintBackdrop (juce::Graphics& g)
{
    const float H = (float) getBaseHeight();
    const auto all = juce::Rectangle<float> ((float) baseWidth, H);
    g.setGradientFill (juce::ColourGradient (Colours::backgroundHi, baseWidth * 0.5f, H * 0.45f,
                                             Colours::background, 0.0f, H, true));
    g.fillAll();

    // Honeycomb wall
    drawHoneycomb (g, all, 30.0f, Colours::accent.withAlpha (0.045f), 1.4f);

    // The necro-bee, looming behind everything
    if (emblem.isValid())
    {
        const float h = 640.0f, w = h * (float) emblem.getWidth() / (float) emblem.getHeight();
        g.setOpacity (0.2f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (emblem, juce::Rectangle<float> (w, h).withCentre ({ baseWidth * 0.5f, H * 0.52f }),
                     juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    drawGrime (g, all, 0.6f);

    // Vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, baseWidth * 0.5f, H * 0.5f,
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

    // Footswitch plate (pedalboard strip behind the stomps and LINK switches) and the scene strip
    drawPanel (g, footswitchArea, 12.0f);
    drawPanel (g, sceneBar.getBounds().toFloat(), 10.0f);

    g.setFont (font (13.0f));
    g.setColour (Colours::textFaint);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<float> (16.0f, H - 22.0f, 120.0f, 16.0f),
                juce::Justification::centredLeft, false);

    auto titleRow = [] (juce::Rectangle<float> a) { return a.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f); };

    if (mini)
        return;

    if (currentPage == fxPageIndex)
    {
        for (auto a : { swarmArea, fuzzArea, flowArea })
            drawPanel (g, a);
        drawSectionTitle (g, titleRow (fuzzArea),  "SMOKE", lastSectionStates[3]);
        drawSectionTitle (g, titleRow (swarmArea), "SWARM", lastSectionStates[2]);
        drawSectionTitle (g, titleRow (flowArea),  "WINGS", lastSectionStates[4]);
    }

    if (currentPage == pitchPageIndex)
    {
        drawPanel (g, hiveArea);
        drawPanel (g, shiftArea);

        drawSectionTitle (g, titleRow (hiveArea), "HIVE", lastSectionStates[1]);
        g.setFont (font (12.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("harmonies and pitch-shifting repeats  -  VENOM switches it on while held", titleRow (hiveArea).withTrimmedLeft (86.0f),
                    juce::Justification::centredLeft, false);

        static const char* sectionNames[] { "VOICES", "TRAILS", "MANGLE" };
        for (size_t i = 0; i < hiveSections.size(); ++i)
        {
            const auto& sec = hiveSections[i];
            if (i > 0)
            {
                g.setGradientFill (juce::ColourGradient (Colours::panelBorder.withAlpha (0.0f), sec.getX(), sec.getY() + 36.0f,
                                                         Colours::panelBorder, sec.getX(), sec.getBottom() - 20.0f, false));
                g.fillRect (juce::Rectangle<float> (sec.getX() - 0.5f, sec.getY() + 40.0f, 1.0f, sec.getHeight() - 52.0f));
            }
            g.setFont (displayFont (16.0f));
            g.setColour (lastSectionStates[1] ? Colours::accent : Colours::textDim);
            g.drawText (sectionNames[i], juce::Rectangle<float> (sec.getX() + 16.0f, sec.getY() + 36.0f, 120.0f, 22.0f),
                        juce::Justification::centredLeft, false);
        }

        drawSectionTitle (g, titleRow (shiftArea), "SHIFT", lastSectionStates[0]);
        g.setFont (font (12.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("pitch shifter  -  on = SHIFT A all the time, footswitches engage it while held", titleRow (shiftArea).withTrimmedLeft (86.0f),
                    juce::Justification::centredLeft, false);
    }
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
    pitchScope.push (meters.pitchSemitones.load(), noiseOn, meters.stackOn.load(), meters.stackSemitones.load());
    // Footswitch captions follow the SHIFT intervals
    const int a = juce::roundToInt (state.getRawParameterValue (ParamIDs::shiftA)->load());
    const int b = juce::roundToInt (state.getRawParameterValue (ParamIDs::shiftB)->load());
    if (a != lastShiftA || b != lastShiftB)
    {
        lastShiftA = a;
        lastShiftB = b;
        oct1Switch.setCaption (ParamChoices::shiftCaption (a));
        oct2Switch.setCaption (ParamChoices::shiftCaption (b));
    }

    presetBar.refresh();
    sceneBar.refresh();
    chainStrip.refresh();
    eqPage.tick();
    reverbPage.tick();

    // MIDI learn: the control waiting for a message pulses
    const auto learning = processor.getMidiLearnParam();
    for (auto* f : { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch })
        f->setLearning (learning.isNotEmpty() && MidiLearnable::paramOf (*f) == learning);
    auto* target = learning.isNotEmpty() ? findLearnable (learning) : nullptr;
    if (target != nullptr && dynamic_cast<Footswitch*> (target) == nullptr)
    {
        learnMarker.setBounds (getLocalArea (target, target->getLocalBounds()).expanded (4));
        learnMarker.phase += 0.12f;
        learnMarker.setVisible (true);
        learnMarker.toFront (false);
        learnMarker.repaint();
    }
    else
    {
        learnMarker.setVisible (false);
    }

    const bool synced = paramOn (ParamIDs::flowSync);
    flowSpeedKnob.setVisible (! synced);
    flowDivKnob.setVisible (synced);
    const bool hiveSynced = paramOn (ParamIDs::rbSync);
    rbTimeKnob.setVisible (! hiveSynced);
    rbDivKnob.setVisible (hiveSynced);

    const bool voicesOn = paramOn (ParamIDs::rbOn) || venom;
    const std::array<bool, 5> states { noiseOn, voicesOn, paramOn (ParamIDs::swarmOn),
                                       paramOn (ParamIDs::fuzzOn), paramOn (ParamIDs::flowOn) };

    setSectionDimmed ({ &snapToggle, &pitchKnob, &primaryKnob, &secondaryKnob, &trackingKnob, &magicKnob,
                        &hvMangleKnob, &rbDetuneKnob, &rbMixKnob, &rbRawToggle }, ! states[1]);
    // STEPS / TIME / TONE / GATE only matter once there are repeats: TRAILS up (or VENOM held)
    const bool trailsAudible = states[1] && (state.getRawParameterValue (ParamIDs::rbMagic)->load() > 0.5f || venom);
    setSectionDimmed ({ &rbSyncToggle, &trDryToggle, &rbTimeKnob, &rbDivKnob, &toneKnob, &gateKnob, &stepGrid }, ! trailsAudible);
    stepGrid.refresh (trailsAudible ? meters.trailStep.load() : -1);
    setSectionDimmed ({ &deepToggle, &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob }, ! states[2]);
    setSectionDimmed ({ &fuzzVoiceSelector, &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob,
                        &fuzzGlareKnob, &fuzzGateKnob, &fuzzSagKnob, &fuzzBlendKnob }, ! states[3]);
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

    setResizable (true, true);
    const float scale = juce::jlimit (0.7f, 2.0f, swarmProcessor.getUiScale());
    applyMode (scale);
    panel.onModeChanged = [this] { applyMode ((float) getWidth() / (float) MainPanel::baseWidth); };

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
    panel.setBounds (0, 0, MainPanel::baseWidth, panel.getBaseHeight());
    swarmProcessor.setUiScale (scale);
}

//==============================================================================
void MainPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    for (auto* c = e.originalComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        const auto id = MidiLearnable::paramOf (*c);
        if (id.isNotEmpty())
        {
            showMidiMenu (id, c);
            return;
        }
    }
}

juce::Component* MainPanel::findLearnable (const juce::String& paramID)
{
    std::function<juce::Component* (juce::Component&)> search = [&] (juce::Component& parent) -> juce::Component*
    {
        for (auto* child : parent.getChildren())
        {
            if (MidiLearnable::paramOf (*child) == paramID && child->isShowing())
                return child;
            if (auto* found = search (*child))
                return found;
        }
        return nullptr;
    };
    return search (*this);
}

void MainPanel::showMidiMenu (const juce::String& paramID, juce::Component* target, int value)
{
    auto* param = state.getParameter (paramID);
    if (param == nullptr)
        return;
    auto& proc = processor;
    const bool isScene = paramID == ParamIDs::scene && value >= 0;
    const juce::String what = isScene ? "Scene " + ParamChoices::scenes[value] : param->getName (40);
    const auto bound = proc.describeMidiBinding (paramID, value);
    juce::PopupMenu menu;
    menu.addSectionHeader (what + (bound.isNotEmpty() ? "  -  MIDI: " + bound : juce::String ("  -  MIDI: not assigned")));
    if (proc.getMidiLearnParam() == paramID && proc.getMidiLearnValue() == value)
        menu.addItem ("Cancel MIDI Learn", [&proc] { proc.cancelMidiLearn(); });
    else
        menu.addItem (bound.isNotEmpty() ? "MIDI Learn another pedal / key / CC" : "MIDI Learn (press a pedal / key or move a CC)",
                      [&proc, paramID, value] { proc.startMidiLearn (paramID, value); });
    menu.addItem ("Clear MIDI", bound.isNotEmpty(), false, [&proc, paramID, value] { proc.clearMidiBindings (paramID, value); });
    menu.addSeparator();
    if (isScene)
    {
        auto& pm = proc.getPresetManager();
        const int current = pm.getCurrentScene();
        menu.addItem ("Copy scene " + ParamChoices::scenes[current] + " here", value != current, false,
                      [&pm, value] { pm.copyCurrentSceneTo (value); });
        menu.addItem ("Scene pedal: selects this scene on every press", false, false, nullptr);
    }
    else
    {
        const bool isSwitch = dynamic_cast<juce::AudioParameterBool*> (param) != nullptr;
        const bool isChoice = dynamic_cast<juce::AudioParameterChoice*> (param) != nullptr;
        const bool isFootswitch = paramID == ParamIDs::oct1 || paramID == ParamIDs::oct2 || paramID == ParamIDs::magicHold;
        menu.addItem (isFootswitch ? "Footswitch: follows MOMENTARY (held = on) / LATCH (press = on / off)"
                      : isSwitch   ? "Switch: every press toggles it - one pedal can drive several switches"
                      : isChoice   ? "Selector: every press steps to the next option"
                                   : "Knob: follows the CC value (0..127)", false, false, nullptr);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void MainPanel::LearnMarker::paint (juce::Graphics& g)
{
    const float a = 0.45f + 0.4f * std::sin (phase);
    g.setColour (Colours::accentBright.withAlpha (a));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f, 2.5f);
}

void SwarmnessAudioProcessorEditor::applyMode (float scale)
{
    // full view or MINI: same width, different height (the aspect ratio follows)
    const int h = panel.getBaseHeight();
    setResizeLimits ((int) (MainPanel::baseWidth * 0.7), (int) (h * 0.7), MainPanel::baseWidth * 2, h * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainPanel::baseWidth / (double) h);
    setSize (juce::roundToInt (MainPanel::baseWidth * scale), juce::roundToInt ((float) h * scale));
    resized();
}
