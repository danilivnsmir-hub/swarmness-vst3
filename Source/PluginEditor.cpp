#include "PluginEditor.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

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
      wasp (p),
      ampCab (p),
      presetBar (p.getPresetManager()),
      tunerOverlay (p.getTuner()),
      switchModeSelector (param (state, ParamIDs::switchMode), { "MOMENTARY", "LATCH" }),
      stepGrid (p.getAPVTS(), StepGrid::trails()),
      wingsGrid (p.getAPVTS(), StepGrid::wings()),
      licencePanel (p),
      fuzzVoiceSelector (param (state, ParamIDs::fuzzVoice), { "DOWN", "MID", "UP" }),
      oct1Switch   (param (state, ParamIDs::oct1),      "SHIFT A", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      oct2Switch   (param (state, ParamIDs::oct2),      "SHIFT B", Colours::accent,  false, [this] { return footswitchesMomentary(); }),
      magicSwitch  (param (state, ParamIDs::magicHold), "VENOM",  juce::Colour (0xffb46bff), false, [this] { return footswitchesMomentary(); }),
      stingSwitch  (param (state, ParamIDs::stingHold), "STING",  juce::Colour (0xff5fd36b), false, [this] { return footswitchesMomentary(); }),
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
    for (auto& page : pages)
    {
        page.setInterceptsMouseClicks (false, true);
        addChildComponent (page);
    }
    addChildComponent (wasp);
    addChildComponent (ampCab);
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
            showMidiMenu (id, &chainStrip, -1, block);
    };
    chainStrip.isBlockActive = [this] (int block)
    {
        return (block == Chain::shift && processor.getMeters().noiseEngaged.load()) || blockEngaged (block);
    };
    // the tiles' second line: what the block is set to, where one word says it
    chainStrip.subtitleFor = [this] (int block) -> juce::String
    {
        using namespace ParamIDs;
        auto index = [this] (const char* id) { return juce::roundToInt (state.getRawParameterValue (id)->load()); };
        auto pick = [] (const juce::StringArray& names, int i) { return names[juce::jlimit (0, names.size() - 1, i)].toUpperCase(); };
        switch (block)
        {
            case Chain::amp:   { const int ch = index (ampChannel); return ch >= 3 ? juce::String ("NAM") : juce::String (AmpBlock::channelModel (ch)); }
            case Chain::cab:   return pick (ParamChoices::cabTypes, index (cabType));
            case Chain::drive: return paramOn (drvNam) ? juce::String ("NAM") : pick (ParamChoices::waspCharacters, index (drvCharacter));
            case Chain::crypt: return pick (ParamChoices::reverbTypes, index (revType));
            case Chain::smoke: return pick (ParamChoices::fuzzVoices, index (fuzzVoice)) + " FUZZ";
            case Chain::shift:
            {
                auto st = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v); };
                return st (index (shiftA)) + " / " + st (index (shiftB));
            }
            default:           return {};
        }
    };
    chainStrip.stompMarksFor = [this] (int block)
    {
        using namespace ParamIDs;
        auto wired = [this] (const char* id) { return id != nullptr && state.getRawParameterValue (id)->load() > 0.5f; };
        if (block == Chain::shift)
            return (wired (linkOct1) || wired (linkOct2) ? 1 : 0) | (wired (stingShiftA) || wired (stingShiftB) ? 2 : 0);
        return (wired (venomBlock[block]) ? 1 : 0) | (wired (stingBlock[block]) ? 2 : 0);
    };
    addAndMakeVisible (chainStrip);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (switchModeSelector);
    switchModeSelector.setTooltip ("Footswitches: MOMENTARY = active only while held, LATCH = click on / click off");
    addAndMakeVisible (infoButton);
    infoButton.setTooltip ("Help");
    infoButton.onClick = [this]
    {
        if (mini)
            setMini (false);   // the help needs the full window
        infoOverlay.setVisible (true);
        infoOverlay.toFront (false);
    };
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

    // SHIFT (footswitch shifter: the power button keeps SHIFT A on, the footswitches engage it while held)
    attachButton (pages[shiftPage], shiftPower, shOn, "SHIFT on / off: on = SHIFT A sounds all the time (like a latched pedal). "
                                               "The SHIFT A / B footswitches still engage it while held when it is off"); 
    shiftAKnob.attach (state, shiftA, "SHIFT A: interval of the SHIFT A footswitch, -24..+24 semitones (octaves, fifths, fourths...)");
    shiftBKnob.attach (state, shiftB, "SHIFT B: interval of the SHIFT B footswitch (while both are held it wins - or both sound with STACK)");
    riseKnob  .attach (state, rise,   "RISE: time to glide into the interval when a SHIFT footswitch goes down");
    fallKnob  .attach (state, fall,   "FALL: time to glide back home when the footswitch is released");
    blendKnob .attach (state, stingMix, "MIX: dry / shifted while SHIFT is engaged. 50% = both at full level (doubled), 100% = only the shifted note");
    panicKnob .attach (state, panic,  "ANGER: a second voice detuned against the shifted note - sour beating, dissonant clusters");
    chaosKnob .attach (state, chaos,  "FRENZY: random pitch jumps, wider and faster as you turn it up (SNAP = on 4ths / 5ths / octaves)");
    speedKnob .attach (state, speed,  "BUZZ: all-pass feedback + amplitude modulation - slow phasing up to ring-mod shrieks");
    shDetuneKnob.attach (state, shDetune, "DETUNE: fine offset of the shifted note, -50..+50 cents - a sour SHIFT");
    attachButton (pages[shiftPage], stackToggle, shStack, "STACK: holding SHIFT A and B together plays both intervals at once (a second voice splits off to B). Off = B wins");
    attachButton (pages[shiftPage], shSnapToggle, shSnap, "SNAP: FRENZY jumps land on 4ths / 5ths / octaves (off = random in-between pitches)");
    attachButton (pages[shiftPage], shRawToggle, shRaw, "RAW: cheap-pedal-DSP character - warble, rough splices, lo-fi converters. Off = clean modern engine");
    pitchScope.setTooltip ("Live SHIFT transposition (with FRENZY / ANGER movement)");

    // HIVE - VOICES
    attachButton (pages[hivePage], hivePower, rbOn, "HIVE on / off (voices + trails). The VENOM footswitch switches it on while held, like a momentary pedal");
    attachButton (pages[hivePage], snapToggle, rbSnap, "SNAP: PITCH in whole semitones, and FRENZY jumps land on 4ths / 5ths / octaves (off = atonal in-between pitches)");
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
    attachButton (pages[hivePage], trDryToggle, trDry, "DRY: the repeats start from your note instead of the DRONE - HIVE becomes a delay "
                                                 "(HOLD steps = plain echoes, UP / DOWN = a pitch-shifting delay). DRONE / QUEEN still sound on top if turned up");
    attachButton (pages[hivePage], hvStopToggle, hvStop, "STOP: the repeats slow to a halt like a tape (pitch falls with them) and start again when released - "
                                                   "the STOP knob sets how long; wire VENOM or STING to it for a pedal. Right-click: MIDI learn");
    hvStopTimeKnob.attach (state, hvStopTime, "FALL: how long the tape takes to halt after STOP goes down");
    hvStopRiseKnob.attach (state, hvStopRise, "RISE: how long the tape takes to come back up to speed when STOP is released (the live repeats blend back in on the way)");
    pages[hivePage].addAndMakeVisible (hvStopTimeKnob);
    pages[hivePage].addAndMakeVisible (hvStopRiseKnob);
    attachButton (pages[hivePage], rbSyncToggle, rbSync, "SYNC: lock the repeats to the host tempo - while the song plays, the STEPS follow the bar grid");
    // MANGLE
    hvMangleKnob.attach (state, hvMangle, "MANGLE: chaos for the voices and trails in one knob - first sour detuned voices (ANGER), "
                                          "then random pitch jumps (FRENZY, on 4ths / 5ths / octaves with SNAP), then all-pass buzz and AM (BUZZ) on top");
    rbDetuneKnob.attach (state, rbDetune, "DETUNE: -50..+50 cents - DRONE up / QUEEN down for width");
    rbMixKnob.attach (state, rbMix, "MIX: dry / HIVE voices. 50% = both at full level, 100% = only the voices and trails");
    attachButton (pages[hivePage], rbRawToggle, rbRaw, "RAW: cheap-pedal-DSP character for the voices - warble, rough splices, lo-fi converters. Off = clean modern engine");

    for (auto* c : std::initializer_list<juce::Component*> { &pitchScope, &shiftAKnob, &shiftBKnob, &riseKnob, &fallKnob, &blendKnob,
                                                             &panicKnob, &chaosKnob, &speedKnob, &shDetuneKnob })
        pages[shiftPage].addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &pitchKnob, &primaryKnob, &secondaryKnob, &trackingKnob,
                                                             &magicKnob, &rbTimeKnob, &toneKnob, &gateKnob, &stepGrid,
                                                             &hvMangleKnob, &rbDetuneKnob, &rbMixKnob })
        pages[hivePage].addAndMakeVisible (c);
    pages[hivePage].addChildComponent (rbDivKnob);

    // HONEY
    attachButton (pages[honeyPage], honeyPower, hnOn, "HONEY sustainer / compressor on / off");
    attachButton (pages[honeyPage], limitToggle, hnLimit, "LIMIT: a fast peak limiter after the compressor (ceiling -6 dBFS, the top of the IN meter's green zone) - stops the picks that a slow ATTACK lets through");
    honeySustainKnob.attach (state, hnSustain, "SUSTAIN: threshold down and ratio up together, like one knob on a pedal - a touch at 0, everything squashed into sticky sustain at 100");
    honeyAttackKnob .attach (state, hnAttack,  "ATTACK: 1 .. 40 ms - slower lets the pick through before the squash");
    honeyBlendKnob  .attach (state, hnBlend,   "BLEND: parallel blend - dry at 0, only the compressed signal at 100");
    honeyLevelKnob  .attach (state, hnLevel,   "LEVEL: output on top of the automatic make-up (SUSTAIN alone keeps the loudness about even)");
    honeyMeter.setTooltip ("Gain reduction: how much HONEY is turning down right now");
    for (auto* c : std::initializer_list<juce::Component*> { &honeySustainKnob, &honeyAttackKnob, &honeyBlendKnob, &honeyLevelKnob, &honeyMeter })
        pages[honeyPage].addAndMakeVisible (c);

    // SWARM
    attachButton (pages[swarmPage], swarmPower, swarmOn,   "SWARM chorus on / off");
    attachButton (pages[swarmPage], deepToggle, swarmDeep, "Deep mode: 8 voices with feedback");
    swarmDepthKnob.attach (state, swarmDepth, "Modulation depth");
    swarmRateKnob .attach (state, swarmRate,  "Modulation rate");
    swarmMixKnob  .attach (state, swarmMix,   "Chorus mix: 50% = dry and chorus both at full level, 100% = pure vibrato");
    swarmRingKnob .attach (state, swarmRing,  "RING: the voices through a ring modulator - a slow tremble at the bottom, metallic bells and clangs up top (40 Hz .. 1.3 kHz); the dry signal stays clean");
    for (auto* c : std::initializer_list<juce::Component*> { &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob, &swarmRingKnob })
        pages[swarmPage].addAndMakeVisible (c);

    // FUZZ
    attachButton (pages[smokePage], fuzzPower, fuzzOn,   "SMOKE fuzz on / off");
    fuzzVoiceSelector.setTooltip ("VOICE: DOWN = doom low-mids and full bottom, MID = jumbo fuzz, UP = tight, screaming upper mids");
    fuzzKnob     .attach (state, fuzz,      "FUZZ: from dirty crunch to wall-of-fuzz sustain");
    fuzzToneKnob .attach (state, fuzzTone,  "TONE: dark <-> bright (also opens the fizz)");
    fuzzScoopKnob.attach (state, fuzzScoop, "SCOOP: mid cut depth - flat mids at 0, deep jumbo-fuzz scoop at max");
    fuzzGlareKnob.attach (state, fuzzGlare, "GLARE: gated octave-up that rips through on hard picking");
    fuzzGateKnob .attach (state, fuzzGate,  "GATE: starve the fuzz - sputtering, gated velcro decay");
    fuzzBlendKnob.attach (state, fuzzBlend, "CLEAN: clean signal added under the full fuzz (pick attack and low end)");
    fuzzSagKnob  .attach (state, fuzzSag,   "SAG: how much the fuzz breathes - the supply sags on the pick (compressed, darker) and the note blooms back as it recovers");
    fuzzCrushKnob.attach (state, fuzzCrush, "CRUSH: fewer bits and a lower sample rate on the fuzz (16 -> 4 bits, 48 -> 4 kHz, aliasing and all) - digital grit on top of the diodes; CLEAN underneath stays clean");
    for (auto* c : std::initializer_list<juce::Component*> { &fuzzVoiceSelector, &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob,
                                                             &fuzzGlareKnob, &fuzzGateKnob, &fuzzSagKnob, &fuzzBlendKnob, &fuzzCrushKnob })
        pages[smokePage].addAndMakeVisible (c);

    // FLOW
    attachButton (pages[wingsPage], flowPower,  flowOn,   "WINGS gate on / off");
    attachButton (pages[wingsPage], hardToggle, flowHard, "Hard stutter gate (off = smooth tremolo)");
    attachButton (pages[wingsPage], syncToggle, flowSync, "Sync to host tempo");
    flowAmountKnob.attach (state, flowAmount, "Gate depth");
    flowSpeedKnob .attach (state, flowSpeed,  "Gate rate (free)");
    flowDivKnob   .attach (state, flowDiv,    "Gate rate (tempo division)");
    pages[wingsPage].addAndMakeVisible (flowAmountKnob);
    pages[wingsPage].addAndMakeVisible (flowSpeedKnob);
    pages[wingsPage].addChildComponent (flowDivKnob);
    pages[wingsPage].addAndMakeVisible (wingsGrid);

    // OUTPUT
    inputKnob .attach (state, input,  "INPUT: input gain - how hard SMOKE, WASP, the AMP and the pitch tracking are hit. Aim for peaks in the green zone of the IN meter");
    addAndMakeVisible (inputKnob);
    inMeter.setTargetZone (-18.0f, -6.0f);
    inMeter.setTooltip ("Input level after INPUT - aim for peaks in the green zone");
    learnButton.setTooltip ("LEARN: play your loudest for 5 seconds - INPUT is set so the peaks land at -12 dBFS");
    learnButton.onClick = [this] { processor.startInputLearn(); };
    addAndMakeVisible (learnButton);
    attachButton (*this, outFreezeToggle, outFreeze, "FREEZE: holds what is sounding right now as a pad (the whole output) while you play on top. "
                                                     "Wire VENOM or STING to it for a pedal. Right-click: MIDI learn");
    attachButton (*this, outStopToggle, outStop, "STOP: a tape stop of everything - the output slows to a halt (pitch falls) and spins back up when released "
                                                 "(Output Stop Time, 1 s by default, is an automatable parameter). Right-click: MIDI learn");
    volumeKnob.attach (state, output, "Output level");
    addAndMakeVisible (volumeKnob);
    inGateKnob.attach (state, inGate, "GATE: one noise gate for the whole chain, right after INPUT, keyed from your guitar (0 = off, then -75 .. -20 dBFS). "
                                      "Opens on the pick a moment early, holds 30 ms, lets a note's tail fade instead of chopping it; shuts fast after a mute");
    addAndMakeVisible (inGateKnob);

    // Footswitches
    oct1Switch  .setTooltip ("SHIFT A: transposes by the SHIFT A interval (hold, or click in LATCH mode) - works even while the plug-in is bypassed. Right-click: MIDI learn");
    oct2Switch  .setTooltip ("SHIFT B: transposes by the SHIFT B interval (wins over A, or adds to it with STACK) - works even while the plug-in is bypassed. Right-click: MIDI learn");
    magicSwitch .setTooltip ("VENOM: a stomp - while held it engages what you wire to it (right-click: per scene). Out of the box: HIVE on and its trails slammed "
                             "into self-oscillation; the LINK switches bring SHIFT A / B along. Works even with the block or the plug-in off. Right-click: wiring, MIDI learn");
    stingSwitch .setTooltip ("STING: a second stomp - right-click to choose, per scene, which blocks it switches on or off while held (a solo boost, a clean drop, "
                             "a freeze...). Works even with the plug-in off. Right-click: wiring, MIDI learn");
    bypassSwitch.setTooltip ("Plug-in on / bypass. The SHIFT, VENOM and STING footswitches still work while bypassed, like momentary pedals. Right-click: MIDI learn");
    attachButton (*this, link1Switch, linkOct1, "LINK: pressing VENOM also engages SHIFT A");
    attachButton (*this, link2Switch, linkOct2, "LINK: pressing VENOM also engages SHIFT B");
    for (auto* c : std::initializer_list<juce::Component*> { &oct1Switch, &oct2Switch, &magicSwitch, &stingSwitch, &bypassSwitch, &inMeter, &outMeter })
        addAndMakeVisible (c);

    learnMarker.setInterceptsMouseClicks (false, false);
    addChildComponent (learnMarker);
    addMouseListener (this, true);   // right-clicks anywhere -> MIDI menu of the control under the mouse

    addChildComponent (infoOverlay);
    addChildComponent (licencePanel);
    addAndMakeVisible (licenceButton);
    licenceButton.setTooltip ("The licence: a 7-day trial, then a key from the store. Click to activate");
    licenceButton.onClick = [this] { licencePanel.refresh(); licencePanel.setVisible (true); licencePanel.toFront (false); };
    processor.getLicence().onChange = [this] { refreshLicence(); };
    refreshLicence();
    if (! processor.getLicence().isAuthorised())
    {
        licencePanel.refresh();
        licencePanel.setVisible (true);
        licencePanel.toFront (false);
    }
    addChildComponent (tunerOverlay);
    addAndMakeVisible (tunerButton);
    tunerButton.setTooltip ("TUNE: open the tuner (MUTE silences the output while it is open)");
    tunerButton.onClick = [this] { tunerOverlay.open(); };

    Knob::alwaysShowValues = processor.getUiValues();
    presetBar.extraMenuItems = [this] (juce::PopupMenu& m)
    {
        m.addItem ("Always show knob values", true, Knob::alwaysShowValues, [this]
        {
            Knob::alwaysShowValues = ! Knob::alwaysShowValues;
            processor.setUiValues (Knob::alwaysShowValues);
            repaint();
        });
       #if JucePlugin_Build_Standalone
        if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        {
            m.addSeparator();
            juce::PopupMenu source;
            static const char* names[] { "Auto (follows the signal)", "Left channel", "Right channel", "Stereo" };
            for (int i = 0; i < 4; ++i)
                source.addItem (names[i], true, (int) processor.getInputSource() == i, [this, i] { processor.setInputSource ((SwarmnessAudioProcessor::InputSource) i); });
            m.addSubMenu ("Input source", source);
            m.addItem ("Audio settings (single channels)...", [this]
            {
                if (auto* holder = juce::StandalonePluginHolder::getInstance())
                {
                    auto* selector = new juce::AudioDeviceSelectorComponent (holder->deviceManager, 0, 2, 0, 2, true, false, false, false);
                    selector->setSize (520, 600);
                    juce::DialogWindow::LaunchOptions o;
                    o.content.setOwned (selector);
                    o.dialogTitle = "Audio settings - one channel at a time";
                    o.dialogBackgroundColour = Colours::panel;
                    o.escapeKeyTriggersCloseButton = true;
                    o.useNativeTitleBar = true;
                    o.resizable = false;
                    o.launchAsync();
                }
            });
        }
       #endif
    };
    // the LINK switches left the footer: VENOM's wiring menu has SHIFT A / B (the parameters are unchanged)
    link1Switch.setVisible (false);
    link2Switch.setVisible (false);

    mini = processor.getUiMini();
    setSize (baseWidth, getBaseHeight());
    showPage (processor.getUiPage());
    tick();
}

MainPanel::~MainPanel()
{
    processor.getLicence().onChange = nullptr;   // no more callbacks into a panel that is gone
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
        case Chain::honey: return honeyPage;
        case Chain::smoke: return smokePage;
        case Chain::shift: return shiftPage;
        case Chain::pitch: return hivePage;
        case Chain::drive: return waspPage;
        case Chain::amp:
        case Chain::cab:   return rigPage;
        case Chain::swarm: return swarmPage;
        case Chain::wings: return wingsPage;
        case Chain::comb:  return combPage;
        case Chain::carve: return carvePage;
        case Chain::crypt: return cryptPage;
        default:           return rigPage;
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
    currentPage = page < 0 || page >= numPages ? (int) rigPage : page;
    processor.setUiPage (currentPage);
    for (int i = 0; i < numPages; ++i)
        pages[(size_t) i].setVisible (! mini && currentPage == i);
    wasp.setVisible (! mini && currentPage == waspPage);
    ampCab.setVisible (! mini && currentPage == rigPage);
    eqPage.setView (currentPage == carvePage ? 1 : 0);
    eqPage.setVisible (! mini && (currentPage == combPage || currentPage == carvePage));
    reverbPage.setVisible (! mini && currentPage == cryptPage);
    miniButton.setToggleState (mini, juce::dontSendNotification);
    miniButton.setButtonText (mini ? "FULL" : "MINI");
    if (! eqPage.isVisible())
        processor.getSpectrumTap().setActive (false);

    std::array<bool, Chain::numBlocks> hi {};
    for (int b = 0; b < Chain::numBlocks; ++b)
        hi[(size_t) b] = pageShowsBlock (currentPage, b);
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
    presetBar.setBounds (222, 16, 544, 32);
    switchModeSelector.setBounds (776, 18, 156, 28);
    tunerButton.setBounds (938, 16, 48, 32);
    miniButton.setBounds (baseWidth - 16 - 32 - 8 - 52, 16, 52, 32);
    infoButton.setBounds (baseWidth - 16 - 32, 16, 32, 32);

    // Chain strip (tiles + the PRE / RIG / POST band) and the page area below it: one block at a time.
    // The chain is the navigation, so it gets the room; a page keeps its knobs together in the middle.
    chainStrip.setBounds (16, 70, baseWidth - 32, 84 + (int) ChainStrip::zoneBandHeight);
    pageArea = { 16, 178, baseWidth - 32, 290 };
    for (auto* c : std::initializer_list<juce::Component*> { &eqPage, &reverbPage, &wasp, &ampCab })
        c->setBounds (pageArea);
    for (auto& page : pages)
        page.setBounds (getLocalBounds());

    const auto P = pageArea.toFloat();
    honeyArea = fuzzArea = swarmArea = flowArea = hiveArea = shiftArea = P;

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };
    auto pillFor  = [] (juce::Rectangle<float> a, int slot) { return juce::Rectangle<int> ((int) a.getRight() - 40 - 66 * (slot + 1), (int) a.getY() + 9, 60, 24); };
    constexpr int bigW = 92, bigH = 128, smallW = 70, smallH = 100, bigStep = 124, smallStep = 96;
    // knobs side by side, centred between x0 and x1; big ones and small ones share a centre line
    auto row = [] (std::initializer_list<std::pair<Knob*, bool>> knobs, int x0, int x1, int centreY)
    {
        int total = 0;
        for (auto& k : knobs) total += k.second ? bigStep : smallStep;
        int x = (x0 + x1 - total) / 2;
        for (auto& [k, big] : knobs)
        {
            const int step = big ? bigStep : smallStep, w = big ? bigW : smallW, h = big ? bigH : smallH;
            k->setBounds (x + (step - w) / 2, centreY - h / 2, w, h);
            x += step;
        }
    };
    const int px0 = (int) P.getX() + 24, px1 = (int) P.getRight() - 24, py = (int) P.getY();
    const int bodyTop = py + 44, bodyBottom = (int) P.getBottom() - 12, bodyMid = (bodyTop + bodyBottom) / 2;

    // HONEY: four knobs, the gain-reduction bar under them
    honeyPower.setBounds (powerFor (honeyArea));
    limitToggle.setBounds (pillFor (honeyArea, 0));
    row ({ { &honeySustainKnob, true }, { &honeyAttackKnob, true }, { &honeyBlendKnob, true }, { &honeyLevelKnob, true } }, px0, px1, bodyMid - 20);
    honeyMeter.setBounds ((int) P.getCentreX() - 240, bodyMid + 64, 480, 24);

    // SMOKE: one row - the three that shape it large, the details smaller
    fuzzPower.setBounds (powerFor (fuzzArea));
    fuzzVoiceSelector.setBounds ((int) fuzzArea.getRight() - 40 - 8 - 150, (int) fuzzArea.getY() + 10, 150, 22);
    row ({ { &fuzzKnob, true }, { &fuzzToneKnob, true }, { &fuzzBlendKnob, true }, { &fuzzScoopKnob, false }, { &fuzzGlareKnob, false },
           { &fuzzGateKnob, false }, { &fuzzSagKnob, false }, { &fuzzCrushKnob, false } }, px0, px1, bodyMid);

    // SHIFT: the live pitch display on the left; SHIFT A / B and MIX large, the movement knobs under them
    shiftPower  .setBounds (powerFor (shiftArea));
    stackToggle .setBounds (pillFor (shiftArea, 2));
    shSnapToggle.setBounds (pillFor (shiftArea, 1));
    shRawToggle .setBounds (pillFor (shiftArea, 0));
    pitchScope.setBounds (px0 - 4, bodyTop + 4, 300, bodyBottom - bodyTop - 4);
    row ({ { &shiftAKnob, true }, { &shiftBKnob, true }, { &blendKnob, true } }, px0 + 310, px1, bodyTop + bigH / 2);
    row ({ { &riseKnob, false }, { &fallKnob, false }, { &panicKnob, false }, { &chaosKnob, false }, { &speedKnob, false }, { &shDetuneKnob, false } },
         px0 + 310, px1, bodyBottom - smallH / 2);

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
        const int y1 = (int) hiveArea.getY() + 64, y2 = (int) hiveArea.getY() + 176, kw = 72, kh = 102;
        auto sectionRow = [&] (juce::Rectangle<float> sec, int y, std::initializer_list<juce::Component*> comps, int slots = 3)
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
        sectionRow (voiceSec, y1, { &pitchKnob, &primaryKnob, &secondaryKnob });
        sectionRow (voiceSec, y2, { &trackingKnob, nullptr, nullptr });
        snapToggle.setBounds ((int) voiceSec.getRight() - 12 - 60, (int) voiceSec.getY() + 36, 60, 22);

        rbSyncToggle.setBounds ((int) trailSec.getRight() - 12 - 60, (int) trailSec.getY() + 36, 60, 22);
        trDryToggle .setBounds ((int) trailSec.getRight() - 12 - 126, (int) trailSec.getY() + 36, 60, 22);
        hvStopToggle.setBounds ((int) trailSec.getRight() - 12 - 192, (int) trailSec.getY() + 36, 60, 22);
        {
            const int kx = (int) trailSec.getX() + 12, ky = y1, sw = 62, sh = 112;
            magicKnob     .setBounds (kx,          ky,      sw, 100);
            rbTimeKnob    .setBounds (kx + sw,     ky,      sw, 100);
            hvStopTimeKnob.setBounds (kx + 2 * sw, ky,      sw, 100);
            toneKnob      .setBounds (kx,          ky + sh, sw, 100);
            gateKnob      .setBounds (kx + sw,     ky + sh, sw, 100);
            hvStopRiseKnob.setBounds (kx + 2 * sw, ky + sh, sw, 100);
            rbDivKnob.setBounds (rbTimeKnob.getBounds());
            const int gx = kx + 3 * sw + 14;
            stepGrid.setBounds (gx, y1, (int) trailSec.getRight() - 14 - gx, (int) hiveArea.getBottom() - 12 - y1);
        }

        rbRawToggle.setBounds ((int) mangleSec.getRight() - 12 - 60, (int) mangleSec.getY() + 36, 60, 22);
        sectionRow (mangleSec, y1, { &hvMangleKnob }, 1);
        sectionRow (mangleSec, y2, { &rbDetuneKnob, &rbMixKnob }, 2);
    }

    // SWARM: four knobs
    swarmPower.setBounds (powerFor (swarmArea));
    deepToggle.setBounds (pillFor (swarmArea, 0));
    row ({ { &swarmDepthKnob, true }, { &swarmRateKnob, true }, { &swarmMixKnob, true }, { &swarmRingKnob, true } }, px0, px1, bodyMid);

    // WINGS: depth and rate on the left, the step pattern next to them
    flowPower.setBounds (powerFor (flowArea));
    syncToggle.setBounds ((int) flowArea.getRight() - 44 - 54,  (int) flowArea.getY() + 9, 54, 24);
    hardToggle.setBounds ((int) flowArea.getRight() - 44 - 112, (int) flowArea.getY() + 9, 54, 24);
    {
        flowAmountKnob.setBounds (px0, bodyMid - bigH / 2, bigW, bigH);
        flowSpeedKnob.setBounds (px0 + bigStep, bodyMid - bigH / 2, bigW, bigH);
        flowDivKnob.setBounds (flowSpeedKnob.getBounds());
        const int gx = flowSpeedKnob.getRight() + 28;
        wingsGrid.setBounds (gx, bodyTop, px1 - gx, bodyBottom - bodyTop);
    }

    // Footer: footswitches centred (LINK mini switches beside the octaves), meters at the sides
    {
        // scenes above the footswitches (full view: under the pages)
        sceneBar.setBounds (16, mini ? 178 : 476, baseWidth - 32, 40);
        const int fy = mini ? 230 : 528, fw = 92, fh = 118, spacing = 120;
        const int total = spacing * 4 + fw;
        int x = (baseWidth - total) / 2;
        for (auto* f : { &oct1Switch, &oct2Switch, &magicSwitch, &stingSwitch, &bypassSwitch })
        {
            f->setBounds (x, fy, fw, fh);
            x += spacing;
        }
        link1Switch.setBounds (oct1Switch.getRight() + 2, fy + 34, 38, 50);
        link2Switch.setBounds (oct2Switch.getRight() + 2, fy + 34, 38, 50);
        footswitchArea = juce::Rectangle<float> ((float) oct1Switch.getX() - 14.0f, (float) fy - 4.0f,
                                                 (float) (bypassSwitch.getRight() - oct1Switch.getX()) + 28.0f, (float) fh + 6.0f);
        inputKnob .setBounds (14, fy - 6, 62, 100);
        inGateKnob.setBounds (80, fy - 6, 62, 100);
        inMeter   .setBounds (150, fy + 48, 90, 26);
        learnButton.setBounds (150, fy + 80, 64, 18);
        volumeKnob.setBounds (baseWidth - 16 - 72, fy - 8, 72, 104);
        outMeter  .setBounds (baseWidth - 16 - 72 - 8 - 140, fy + 48, 140, 26);
        outFreezeToggle.setBounds (outMeter.getX(), fy + 80, 66, 18);
        outStopToggle  .setBounds (outMeter.getX() + 74, fy + 80, 66, 18);
    }

    infoOverlay.setBounds (getLocalBounds());
    licencePanel.setBounds (getLocalBounds());
    licenceButton.setBounds (76, getBaseHeight() - 25, 170, 20);
    tunerOverlay.setBounds (getLocalBounds());
    backdrop.setBounds (getLocalBounds());
}

//==============================================================================
void MainPanel::paintBackdrop (juce::Graphics& g)
{
    const float H = (float) getBaseHeight();
    const auto all = juce::Rectangle<float> ((float) baseWidth, H);
    const bool wallArt = Skin::has ("bg_main");
    if (wallArt)
    {
        // the full view's wall (the mini view shows its top)
        g.fillAll (Colours::background);
        Skin::drawFitted (g, "bg_main", juce::Rectangle<float> ((float) baseWidth, (float) baseHeight),
                          juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (Colours::backgroundHi, baseWidth * 0.5f, H * 0.45f,
                                                 Colours::background, 0.0f, H, true));
        g.fillAll();

        // Honeycomb wall
        drawHoneycomb (g, all, 30.0f, Colours::accent.withAlpha (0.045f), 1.4f);
    }

    // The necro-bee, looming behind everything (the artwork is already drawn faint)
    if (! Skin::drawFitted (g, "bg_emblem", juce::Rectangle<float> (640.0f * 1000.0f / 1290.0f, 640.0f).withCentre ({ baseWidth * 0.5f, H * 0.52f })))
        if (emblem.isValid())
        {
            const float h = 640.0f, w = h * (float) emblem.getWidth() / (float) emblem.getHeight();
            g.setOpacity (0.2f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (emblem, juce::Rectangle<float> (w, h).withCentre ({ baseWidth * 0.5f, H * 0.52f }),
                         juce::RectanglePlacement::centred);
            g.setOpacity (1.0f);
        }

    if (! wallArt)
    {
        drawGrime (g, all, 0.6f);

        // Vignette
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, baseWidth * 0.5f, H * 0.5f,
                                                 juce::Colours::black.withAlpha (0.75f), 0.0f, 0.0f, true));
        g.fillAll();
    }

    // Header
    {
        const auto header = juce::Rectangle<float> (0.0f, 0.0f, (float) baseWidth, 64.0f);
        // artwork: the bar's plain middle stretches so its honey edge lands on the header's bottom,
        // the drips below it hang over the top of the page
        const float barTop = 16.0f, barBottom = 64.0f;   // image rows of the plain middle
        const auto bar = Skin::naturalSize ("header_bar");
        const float drips = (bar.getHeight() / Skin::unitsPerPixel - barBottom) * Skin::unitsPerPixel;   // honey edge + drips, UI units
        if (! Skin::drawSliced (g, "header_bar", header.withHeight (header.getBottom() - 9.0f + drips),
                                { barTop, 0.0f, bar.getHeight() / Skin::unitsPerPixel - barBottom, 0.0f }))
        {
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
        }

        const float logoH = 70.0f;   // breaks out of the header a little, like the pedal's artwork
        if (! Skin::drawFitted (g, "logo", juce::Rectangle<float> (10.0f, 1.0f, 200.0f, logoH), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid))
            if (logo.isValid())
            {
                const float lw = logoH * (float) logo.getWidth() / (float) logo.getHeight();
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (logo, juce::Rectangle<float> (10.0f, 1.0f, lw, logoH), juce::RectanglePlacement::centred);
            }
    }

    // Footswitch plate (pedalboard strip behind the stomps and LINK switches) and the scene strip
    drawPanel (g, footswitchArea, 12.0f, true);
    drawPanel (g, sceneBar.getBounds().toFloat(), 10.0f);

    g.setFont (font (13.0f));
    g.setColour (Colours::textFaint);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<float> (16.0f, H - 22.0f, 120.0f, 16.0f),
                juce::Justification::centredLeft, false);

    auto titleRow = [] (juce::Rectangle<float> a) { return a.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f); };

    if (mini)
        return;

    // the panel draws the frames of the blocks it lays out itself (the other pages paint their own)
    auto blockPanel = [&] (juce::Rectangle<float> a, const char* title, const char* description, bool on)
    {
        drawPanel (g, a);
        drawSectionTitle (g, titleRow (a), title, on);
        g.setFont (font (12.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText (description, titleRow (a).withTrimmedLeft (22.0f + (float) juce::GlyphArrangement::getStringWidthInt (displayFont (23.0f), title) + 12.0f),
                    juce::Justification::centredLeft, false);
    };

    switch (currentPage)
    {
        case honeyPage:
            blockPanel (honeyArea, "HONEY", "sustainer / compressor  -  even picking and long notes in front of everything; LIMIT catches the peaks", lastSectionStates[5]);
            break;
        case smokePage:
            blockPanel (fuzzArea, "SMOKE", "fuzz  -  VOICE picks where the mids sit, CLEAN keeps the pick attack under the wall", lastSectionStates[3]);
            break;
        case shiftPage:
            blockPanel (shiftArea, "SHIFT", "pitch shifter  -  on = SHIFT A all the time, the footswitches engage it while held", lastSectionStates[0]);
            break;
        case swarmPage:
            blockPanel (swarmArea, "SWARM", "chorus  -  DEEP = eight voices with feedback, RING sends the voices through a ring modulator", lastSectionStates[2]);
            break;
        case wingsPage:
            blockPanel (flowArea, "WINGS", "tremolo / stutter gate  -  draw the steps, HARD for a gate, SYNC to the host tempo", lastSectionStates[4]);
            break;
        case hivePage:
        {
            blockPanel (hiveArea, "HIVE", "harmonies and pitch-shifting repeats  -  VENOM switches it on while held", lastSectionStates[1]);
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
            break;
        }
        default:
            break;
    }
}

//==============================================================================
void MainPanel::tick()
{
    auto& meters = processor.getMeters();
    inMeter .update (meters.input[0].exchange (0.0f),  meters.input[1].exchange (0.0f));
    outMeter.update (meters.output[0].exchange (0.0f), meters.output[1].exchange (0.0f));
    outMeter.setLimiting (meters.limiterGr.exchange (0.0f));

    const bool noiseOn = meters.noiseEngaged.load();

    // Octave LEDs also light when a stomp drags them in through LINK.
    const bool venom = paramOn (ParamIDs::magicHold), sting = paramOn (ParamIDs::stingHold);
    oct1Switch.setLitExternally ((venom && paramOn (ParamIDs::linkOct1)) || (sting && paramOn (ParamIDs::stingShiftA)));
    oct2Switch.setLitExternally ((venom && paramOn (ParamIDs::linkOct2)) || (sting && paramOn (ParamIDs::stingShiftB)));
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
    if (wasp.isVisible())
        wasp.tick();
    if (ampCab.isVisible())
        ampCab.tick();

    // MIDI learn: the control waiting for a message pulses
    const auto learning = processor.getMidiLearnParam();
    for (auto* f : { &oct1Switch, &oct2Switch, &magicSwitch, &stingSwitch, &bypassSwitch })
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

    learnButton.setButtonText (processor.isInputLearning() ? "PLAY..." : "LEARN");
    const bool synced = paramOn (ParamIDs::flowSync);
    flowSpeedKnob.setVisible (! synced);
    flowDivKnob.setVisible (synced);
    const bool hiveSynced = paramOn (ParamIDs::rbSync);
    rbTimeKnob.setVisible (! hiveSynced);
    rbDivKnob.setVisible (hiveSynced);

    const bool voicesOn = blockEngaged (Chain::pitch);
    const std::array<bool, 6> states { noiseOn, voicesOn, blockEngaged (Chain::swarm),
                                       blockEngaged (Chain::smoke), blockEngaged (Chain::wings), blockEngaged (Chain::honey) };
    setSectionDimmed ({ &limitToggle, &honeySustainKnob, &honeyAttackKnob, &honeyBlendKnob, &honeyLevelKnob, &honeyMeter }, ! states[5]);
    honeyMeter.set (states[5] ? meters.honeyGr.load() : 0.0f);

    setSectionDimmed ({ &snapToggle, &pitchKnob, &primaryKnob, &secondaryKnob, &trackingKnob, &magicKnob,
                        &hvMangleKnob, &rbDetuneKnob, &rbMixKnob, &rbRawToggle }, ! states[1]);
    // STEPS / TIME / TONE / GATE only matter once there are repeats: TRAILS up (or VENOM held)
    const bool trailsAudible = states[1] && (state.getRawParameterValue (ParamIDs::rbMagic)->load() > 0.5f || venom || sting);
    setSectionDimmed ({ &rbSyncToggle, &trDryToggle, &hvStopToggle, &hvStopTimeKnob, &hvStopRiseKnob, &rbTimeKnob, &rbDivKnob, &toneKnob, &gateKnob, &stepGrid }, ! trailsAudible);
    stepGrid.refresh (trailsAudible ? meters.trailStep.load() : -1);
    setSectionDimmed ({ &deepToggle, &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob, &swarmRingKnob }, ! states[2]);
    setSectionDimmed ({ &fuzzVoiceSelector, &fuzzKnob, &fuzzToneKnob, &fuzzScoopKnob,
                        &fuzzGlareKnob, &fuzzGateKnob, &fuzzSagKnob, &fuzzBlendKnob, &fuzzCrushKnob }, ! states[3]);
    setSectionDimmed ({ &hardToggle, &syncToggle, &flowAmountKnob, &flowSpeedKnob, &flowDivKnob, &wingsGrid }, ! states[4]);
    wingsGrid.refresh (states[4] ? meters.wingStep.load() : -1);

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

void MainPanel::showMidiMenu (const juce::String& paramID, juce::Component* target, int value, int block)
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
    if (block >= 0)
    {
        // a block's settings travel through the clipboard: to another preset, scene or plug-in instance
        const juce::String name (Chain::names[block]);
        auto& pm = proc.getPresetManager();
        menu.addItem ("Copy " + name + " settings", [&pm, block] { juce::SystemClipboard::copyTextToClipboard (pm.copyBlock (block)); });
        const auto clip = juce::SystemClipboard::getTextFromClipboard();
        menu.addItem ("Paste " + name + " settings", PresetManager::clipboardHoldsBlock (clip, block), false,
                      [&pm, block, clip] { pm.pasteBlock (block, clip); });
        // block presets: the block as set in any factory preset, or saved by the user
        juce::PopupMenu fromFactory;
        for (const auto& src : pm.blockPresetSources (block))
            fromFactory.addItem (src, [&pm, block, src] { pm.applyBlockFromPreset (block, src); });
        menu.addSubMenu (name + " from a factory preset", fromFactory);
        juce::PopupMenu user;
        for (const auto& bp : pm.getUserBlockPresets (block))
        {
            juce::PopupMenu one;
            one.addItem ("Load", [&pm, block, bp] { pm.loadUserBlockPreset (block, bp); });
            one.addItem ("Delete", [&pm, block, bp] { pm.deleteUserBlockPreset (block, bp); });
            user.addSubMenu (bp, one);
        }
        if (user.getNumItems() > 0)
            user.addSeparator();
        juce::Component::SafePointer<MainPanel> safe (this);
        user.addItem ("Save " + name + " settings as...", [safe, block, name]
        {
            if (safe == nullptr) return;
            auto* w = new juce::AlertWindow ("Save " + name + " settings", "A name for these " + name + " settings:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "", "Name");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, block, w] (int r)
            {
                if (safe != nullptr && r == 1)
                    safe->processor.getPresetManager().saveUserBlockPreset (block, w->getTextEditorContents ("name"));
            }), true);
        });
        menu.addSubMenu (name + " presets of yours", user);
        menu.addSeparator();
    }
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
        const bool isFootswitch = isFootswitchParameter (paramID);
        if (paramID == ParamIDs::magicHold || paramID == ParamIDs::stingHold)
            addStompWiring (menu, paramID == ParamIDs::magicHold);
        if (paramID == ParamIDs::outStop)
        {
            // the output tape stop's times (also automatable parameters)
            for (auto [id, title] : { std::pair { ParamIDs::outStopTime, "Fall - time to a halt" }, std::pair { ParamIDs::outStopRise, "Rise - time back up" } })
                if (auto* p = state.getParameter (id))
                {
                    juce::PopupMenu times;
                    const float now = p->convertFrom0to1 (p->getValue());
                    for (float t : { 0.2f, 0.4f, 0.7f, 1.0f, 1.5f, 2.5f })
                        times.addItem (juce::String (t, 1) + " s", true, std::abs (now - t) < 0.05f,
                                       [p, t] { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (t)); p->endChangeGesture(); });
                    menu.addSubMenu (juce::String (title) + "  (" + juce::String (now, 1) + " s)", times);
                }
            menu.addSeparator();
        }
        menu.addItem (isFootswitch ? "Footswitch: follows MOMENTARY (held = on) / LATCH (press = on / off)"
                      : isSwitch   ? "Switch: every press toggles it - one pedal can drive several switches"
                      : isChoice   ? "Selector: every press steps to the next option"
                                   : "Knob: follows the CC value (0..127)", false, false, nullptr);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

bool MainPanel::blockEngaged (int block) const
{
    return (processor.getMeters().engagedBlocks.load (std::memory_order_relaxed) & (1u << block)) != 0;
}

void MainPanel::addStompWiring (juce::PopupMenu& menu, bool venom)
{
    using namespace ParamIDs;
    const int scene = processor.getPresetManager().getCurrentScene();
    menu.addSectionHeader (juce::String (venom ? "VENOM" : "STING") + " engages while held  (scene " + ParamChoices::scenes[scene] + ")");
    auto toggleItem = [&] (const char* id, const juce::String& name)
    {
        if (auto* p = state.getParameter (id))
            menu.addItem (name, true, p->getValue() >= 0.5f, [p] { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); });
    };
    toggleItem (venom ? linkOct1 : stingShiftA, "SHIFT A");
    toggleItem (venom ? linkOct2 : stingShiftB, "SHIFT B");
    auto choiceItem = [&] (const char* id, const juce::String& name)
    {
        auto* p = state.getParameter (id);
        if (p == nullptr) return;
        const int current = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
        juce::PopupMenu sub;
        static const char* labels[] { "-  (its own power button)", "On while held", "Off while held" };
        for (int a = 0; a < 3; ++a)
            sub.addItem (labels[a], true, current == a, [p, a] { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) a)); p->endChangeGesture(); });
        menu.addSubMenu (name + (current == 1 ? "  -  On" : current == 2 ? "  -  Off" : juce::String()), sub, true, nullptr, current != 0);
    };
    for (int b : { Chain::honey, Chain::smoke, Chain::pitch, Chain::drive, Chain::amp, Chain::cab, Chain::swarm, Chain::wings, Chain::comb, Chain::carve, Chain::crypt })
        choiceItem (venom ? venomBlock[b] : stingBlock[b], juce::String (Chain::names[b]) + (b == Chain::pitch ? " (+ self-oscillation)" : ""));
    choiceItem (venom ? venomFreeze : stingFreeze, "CRYPT FREEZE");
    choiceItem (venom ? venomStop : stingStop, "HIVE STOP (tape stop)");
    choiceItem (venom ? venomOutFreeze : stingOutFreeze, "OUTPUT FREEZE (the whole signal)");
    choiceItem (venom ? venomOutStop : stingOutStop, "OUTPUT STOP (tape stop of everything)");
    menu.addSeparator();
}

void MainPanel::refreshLicence()
{
    auto& l = processor.getLicence();
    switch (l.getState())
    {
        case Licence::State::activated: licenceButton.setVisible (false); break;
        case Licence::State::trial:
            licenceButton.setVisible (true);
            licenceButton.setButtonText ("TRIAL  -  " + juce::String (l.trialDaysLeft()) + (l.trialDaysLeft() == 1 ? " DAY LEFT" : " DAYS LEFT"));
            break;
        case Licence::State::expired:
            licenceButton.setVisible (true);
            licenceButton.setButtonText ("TRIAL OVER  -  ACTIVATE");
            break;
    }
    licencePanel.refresh();
}

MainPanel::LicencePanel::LicencePanel (SwarmnessAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, true);
    keyEditor.setTextToShowWhenEmpty ("paste your licence key here", Colours::textFaint);
    keyEditor.setFont (font (16.0f));
    keyEditor.setJustification (juce::Justification::centredLeft);
    addAndMakeVisible (keyEditor);
    for (auto* b : { &activateButton, &deactivateButton, &buyButton, &closeButton })
        addAndMakeVisible (b);
    activateButton.onClick = [this]
    {
        status = "Contacting the store...";
        repaint();
        juce::Component::SafePointer<LicencePanel> safe (this);
        processor.getLicence().activate (keyEditor.getText(), [safe] { if (safe != nullptr) safe->refresh(); });
    };
    deactivateButton.onClick = [this]
    {
        status = "Contacting the store...";
        repaint();
        juce::Component::SafePointer<LicencePanel> safe (this);
        processor.getLicence().deactivate ([safe] { if (safe != nullptr) safe->refresh(); });
    };
    buyButton.onClick = [] { juce::URL (Licence::kStoreUrl).launchInDefaultBrowser(); };
    closeButton.onClick = [this] { setVisible (false); };
}

void MainPanel::LicencePanel::refresh()
{
    auto& l = processor.getLicence();
    const bool activated = l.getState() == Licence::State::activated;
    keyEditor.setVisible (! activated);
    activateButton.setVisible (! activated);
    activateButton.setEnabled (! l.isBusy());
    deactivateButton.setVisible (activated);
    deactivateButton.setEnabled (! l.isBusy());
    buyButton.setVisible (! activated);
    if (! l.isBusy())
        status = l.getMessage();
    repaint();
}

void MainPanel::LicencePanel::resized()
{
    card = getLocalBounds().toFloat().withSizeKeepingCentre (560.0f, 300.0f);
    auto r = card.reduced (28.0f, 24.0f).toNearestInt();
    r.removeFromTop (110);
    keyEditor.setBounds (r.removeFromTop (34));
    r.removeFromTop (14);
    auto buttons = r.removeFromTop (34);
    activateButton.setBounds (buttons.removeFromLeft (150));
    deactivateButton.setBounds (activateButton.getBounds());
    buttons.removeFromLeft (10);
    buyButton.setBounds (buttons.removeFromLeft (170));
    closeButton.setBounds (buttons.removeFromRight (100));
}

void MainPanel::LicencePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.82f));
    Theme::drawPanel (g, card, 12.0f);
    auto r = card.reduced (28.0f, 24.0f);
    auto& l = processor.getLicence();

    g.setFont (displayFont (30.0f));
    const auto titleArea = r.removeFromTop (34.0f);
    g.setGradientFill (honeyGradient (titleArea));
    const juce::String title = l.getState() == Licence::State::activated ? "Licensed" : l.getState() == Licence::State::trial ? "Trial" : "Trial over";
    g.drawText (title, titleArea, juce::Justification::centredLeft, false);
    r.removeFromTop (8.0f);

    g.setFont (font (14.5f));
    g.setColour (Colours::text);
    juce::String body;
    switch (l.getState())
    {
        case Licence::State::activated:
            body = "Swarmness is activated on this machine (key " + l.getKey().substring (0, 8) + "...). "
                   "DEACTIVATE frees the activation for another computer.";
            break;
        case Licence::State::trial:
            body = "Everything works for " + juce::String (Licence::kTrialDays) + " days from the first run - " + juce::String (l.trialDaysLeft())
                 + (l.trialDaysLeft() == 1 ? " day" : " days") + " left. A licence key from the store keeps it going; "
                   "one key activates up to three of your computers.";
            break;
        case Licence::State::expired:
            body = "The " + juce::String (Licence::kTrialDays) + "-day trial is over: the plug-in now passes your signal through untouched. "
                   "Paste a licence key to carry on - one key activates up to three of your computers.";
            break;
    }
    g.drawFittedText (body, r.removeFromTop (60.0f).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
    g.setFont (font (13.0f));
    g.setColour (status.startsWith ("Activated") || status.startsWith ("Deactivated") ? Colours::accentBright : Colours::textDim);
    g.drawFittedText (status, card.reduced (28.0f, 24.0f).removeFromBottom (24.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
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
