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
      patternSelector (param (state, ParamIDs::hivePattern), { "LADDER", "BOUNCE", "SCATTER", "REVERSE", "BLOOM" }),
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
    chainStrip.onBlockClicked = [this] (int block) { showPage (pageForBlock (block)); };
    chainStrip.isBlockActive = [this] (int block) { return block == Chain::pitch && processor.getMeters().noiseEngaged.load(); };
    addAndMakeVisible (chainStrip);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (switchModeSelector);
    switchModeSelector.setTooltip ("Footswitches: MOMENTARY = active only while held, LATCH = click on / click off");
    addAndMakeVisible (infoButton);
    infoButton.setTooltip ("Help");
    infoButton.onClick = [this] { infoOverlay.setVisible (true); infoOverlay.toFront (false); };

    // HIVE - SHIFT
    shiftAKnob.attach (state, shiftA, "SHIFT A: interval of the SHIFT A footswitch, -24..+24 semitones (octaves, fifths, fourths...)");
    shiftBKnob.attach (state, shiftB, "SHIFT B: interval of the SHIFT B footswitch (it wins while both are held)");
    riseKnob  .attach (state, rise,   "RISE: time to glide into the interval when a SHIFT footswitch goes down");
    fallKnob  .attach (state, fall,   "FALL: time to glide back home when the footswitch is released");
    blendKnob .attach (state, stingMix, "BLEND: how much the shifted note replaces your note while SHIFT is held (100% = only the shifted note, 50% = doubled)");
    // VOICES
    attachButton (fxPage, voicesPower, rbOn, "HIVE voices (DRONE / QUEEN + TRAILS) on/off - the VENOM footswitch engages them by itself");
    attachButton (fxPage, snapToggle, rbSnap, "SNAP: PITCH in whole semitones (off = atonal in-between intervals)");
    attachButton (fxPage, followToggle, hiveFollow, "FOLLOW: the voices harmonise the SHIFTed note instead of the note you play");
    pitchKnob    .attach (state, rbPitch,     "PITCH: DRONE interval, -12..+12 semitones (SNAP = whole semitones). Also the step of the TRAILS");
    pitchKnob.setSnap ([this] (double v) { return paramOn (ParamIDs::rbSnap) ? std::round (v) : v; });
    primaryKnob  .attach (state, rbPrimary,   "DRONE: level of the main harmony voice (and its trails)");
    secondaryKnob.attach (state, rbSecondary, "QUEEN: a voice one octave from the DRONE (above for up-shifts, below for down)");
    trackingKnob .attach (state, rbTracking,  "TRACKING: high = tight harmonies, low = lag, long repeating grains and tone clusters");
    // TRAILS
    magicKnob .attach (state, rbMagic, "TRAILS: repeats of the DRONE, moved by PITCH as the PATTERN says (the VENOM footswitch pushes them into self-oscillation)");
    rbTimeKnob.attach (state, rbTime,  "TIME: time between the repeats");
    rbDivKnob .attach (state, rbDiv,   "TIME as a tempo division (SYNC on)");
    toneKnob  .attach (state, rbTone,  "TONE: brightness of the voices and the trails");
    attachButton (fxPage, rbSyncToggle, rbSync, "SYNC: lock the repeats to the host tempo");
    patternSelector.setTooltip ("PATTERN - how the repeats move: LADDER = one more PITCH step each time, BOUNCE = flip between the voice and your note, "
                                "SCATTER = random chord tones, REVERSE = backwards repeats, BLOOM = diffused swelling cloud");
    // MANGLE
    panicKnob.attach (state, panic, "ANGER: sour second voices detuned against the shifted note and the harmonies - beating, dissonant clusters");
    chaosKnob.attach (state, chaos, "FRENZY: random pitch jumps of everything HIVE adds - wider and faster as you turn it up");
    speedKnob.attach (state, speed, "BUZZ: all-pass feedback + amplitude modulation on everything HIVE adds - slow phasing up to ring-mod shrieks");
    rbDetuneKnob.attach (state, rbDetune, "DETUNE: +-50 cents - a sour SHIFT, and DRONE up / QUEEN down for width");
    rbMixKnob.attach (state, rbMix, "MIX: dry / HIVE voices. 50% = both at full level, 100% = only the voices and trails (a held SHIFT still sounds)");
    attachButton (fxPage, rbRawToggle, rbRaw, "RAW: cheap-pedal-DSP character for every HIVE voice - warble, rough splices, lo-fi converters. Off = clean modern engine");

    for (auto* c : std::initializer_list<juce::Component*> { &shiftAKnob, &shiftBKnob, &riseKnob, &fallKnob, &blendKnob,
                                                             &pitchKnob, &primaryKnob, &secondaryKnob, &trackingKnob,
                                                             &magicKnob, &rbTimeKnob, &toneKnob, &patternSelector,
                                                             &panicKnob, &chaosKnob, &speedKnob, &rbDetuneKnob, &rbMixKnob })
        fxPage.addAndMakeVisible (c);
    fxPage.addChildComponent (rbDivKnob);

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
    fuzzBlendKnob.attach (state, fuzzBlend, "BLEND: clean signal under the fuzz (pick attack and low end)");
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
    oct2Switch  .setTooltip ("SHIFT B: transposes by the SHIFT B interval (wins over A) - works even while the plug-in is bypassed. Right-click: MIDI learn");
    magicSwitch .setTooltip ("VENOM: slams the regeneration into self-oscillation - works even with HIVE or the plug-in switched off. LINK switches bring the octaves along. Right-click: MIDI learn");
    bypassSwitch.setTooltip ("Plug-in on / bypass. The octave and VENOM footswitches still work while bypassed, like momentary pedals. Right-click: MIDI learn");
    // MIDI learn: right-click a footswitch
    {
        int t = 0;
        for (auto* f : { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch })
        {
            const int target = t++;
            f->onRightClick = [this, target, f]
            {
                auto& proc = processor;
                juce::PopupMenu menu;
                const auto bound = proc.describeMidiBinding (target);
                menu.addSectionHeader (bound.isNotEmpty() ? "MIDI: " + bound : juce::String ("MIDI: not assigned"));
                if (proc.getMidiLearnTarget() == target)
                    menu.addItem ("Cancel MIDI Learn", [&proc] { proc.cancelMidiLearn(); });
                else
                    menu.addItem ("MIDI Learn (press a pedal / key / CC)", [&proc, target] { proc.startMidiLearn (target); });
                menu.addItem ("Clear MIDI", bound.isNotEmpty(), false, [&proc, target] { proc.clearMidiBinding (target); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (f));
            };
        }
    }
    attachButton (*this, link1Switch, linkOct1, "LINK: pressing VENOM also engages SHIFT A");
    attachButton (*this, link2Switch, linkOct2, "LINK: pressing VENOM also engages SHIFT B");
    for (auto* c : std::initializer_list<juce::Component*> { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch, &inMeter, &outMeter })
        addAndMakeVisible (c);

    addChildComponent (infoOverlay);

    setSize (baseWidth, baseHeight);
    showPage (processor.getUiPage());
    tick();
}

void MainPanel::attachButton (juce::Component& parent, juce::Button& b, const juce::String& id, const juce::String& tooltip)
{
    parent.addAndMakeVisible (b);
    b.setTooltip (tooltip);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (state, id, b));
}

int MainPanel::pageForBlock (int block)
{
    switch (block)
    {
        case Chain::comb:
        case Chain::carve: return eqPageIndex;
        case Chain::crypt: return spacePageIndex;
        default:           return fxPageIndex;
    }
}

void MainPanel::showPage (int page)
{
    currentPage = juce::jlimit (0, numPages - 1, page);
    processor.setUiPage (currentPage);
    fxPage.setVisible (currentPage == fxPageIndex);
    eqPage.setVisible (currentPage == eqPageIndex);
    reverbPage.setVisible (currentPage == spacePageIndex);
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
    switchModeSelector.setBounds (822, 18, 196, 28);
    infoButton.setBounds (baseWidth - 16 - 32, 16, 32, 32);

    // Chain strip and the page area below it
    chainStrip.setBounds (16, 70, baseWidth - 32, 66);
    const auto pageArea = juce::Rectangle<int> (16, 144, baseWidth - 32, 464);
    eqPage.setBounds (pageArea);
    reverbPage.setBounds (pageArea);
    fxPage.setBounds (getLocalBounds());

    // FX page section areas
    hiveArea = { 16.0f, 144.0f, (float) baseWidth - 32.0f, 262.0f };
    const float rowY = 418.0f, rowH = 190.0f;
    swarmArea  = { 16.0f,  rowY, 250.0f, rowH };
    fuzzArea   = { 278.0f, rowY, 500.0f, rowH };
    flowArea   = { 790.0f, rowY, 294.0f, rowH };

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };
    auto pillFor  = [] (juce::Rectangle<float> a, int slot) { return juce::Rectangle<int> ((int) a.getRight() - 40 - 66 * (slot + 1), (int) a.getY() + 9, 60, 24); };

    // HIVE: four sections side by side, two rows of knobs each
    {
        const float widths[4] { 262.0f, 262.0f, 272.0f, 0.0f };
        float x = hiveArea.getX();
        for (int i = 0; i < 4; ++i)
        {
            const float w = i < 3 ? widths[i] : hiveArea.getRight() - x;
            hiveSections[(size_t) i] = { x, hiveArea.getY(), w, hiveArea.getHeight() };
            x += w;
        }
        const int y1 = (int) hiveArea.getY() + 58, y2 = (int) hiveArea.getY() + 158, kw = 78, kh = 100;
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
        const auto& shiftSec = hiveSections[0];
        const auto& voiceSec = hiveSections[1];
        const auto& trailSec = hiveSections[2];
        const auto& mangleSec = hiveSections[3];

        row (shiftSec, y1, { &shiftAKnob, &shiftBKnob }, 2);
        row (shiftSec, y2, { &riseKnob, &fallKnob, &blendKnob });

        voicesPower.setBounds ((int) voiceSec.getRight() - 40, (int) voiceSec.getY() + 36, 24, 24);
        row (voiceSec, y1, { &pitchKnob, &primaryKnob, &secondaryKnob });
        row (voiceSec, y2, { &trackingKnob, nullptr, nullptr });
        snapToggle  .setBounds ((int) voiceSec.getX() + 112, y2 + 22, 64, 24);
        followToggle.setBounds ((int) voiceSec.getX() + 182, y2 + 22, 72, 24);

        rbSyncToggle.setBounds ((int) trailSec.getRight() - 12 - 60, (int) trailSec.getY() + 36, 60, 22);
        row (trailSec, y1, { &magicKnob, &rbTimeKnob, &toneKnob });
        rbDivKnob.setBounds (rbTimeKnob.getBounds());
        patternSelector.setBounds ((int) trailSec.getX() + 12, y2 + 30, (int) trailSec.getWidth() - 24, 26);

        rbRawToggle.setBounds ((int) mangleSec.getRight() - 16 - 60, (int) mangleSec.getY() + 36, 60, 22);
        row (mangleSec, y1, { &panicKnob, &chaosKnob, &speedKnob });
        row (mangleSec, y2, { &rbDetuneKnob, &rbMixKnob }, 2);
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
        const int fy = 620, fw = 92, fh = 118, spacing = 136;
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

    // Footswitch plate (pedalboard strip behind the stomps and LINK switches)
    drawPanel (g, footswitchArea, 12.0f);

    g.setFont (font (13.0f));
    g.setColour (Colours::textFaint);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<float> (16.0f, (float) baseHeight - 22.0f, 120.0f, 16.0f),
                juce::Justification::centredLeft, false);

    if (currentPage != fxPageIndex)
        return;

    for (auto a : { hiveArea, swarmArea, fuzzArea, flowArea })
        drawPanel (g, a);

    auto titleRow = [] (juce::Rectangle<float> a) { return a.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f); };

    drawSectionTitle (g, titleRow (hiveArea),    "HIVE",    lastSectionStates[0]);
    g.setFont (font (12.5f, true));
    g.setColour (Colours::textFaint);
    g.drawText ("destructive pitch-delay  -  hold SHIFT A / B below, stomp VENOM", titleRow (hiveArea).withTrimmedLeft (86.0f),
                juce::Justification::centredLeft, false);

    // HIVE sections: small headings and separators
    static const char* sectionNames[] { "SHIFT", "VOICES", "TRAILS", "MANGLE" };
    for (size_t i = 0; i < hiveSections.size(); ++i)
    {
        const auto& sec = hiveSections[i];
        if (i > 0)
        {
            g.setGradientFill (juce::ColourGradient (Colours::panelBorder.withAlpha (0.0f), sec.getX(), sec.getY() + 36.0f,
                                                     Colours::panelBorder, sec.getX(), sec.getBottom() - 20.0f, false));
            g.fillRect (juce::Rectangle<float> (sec.getX() - 0.5f, sec.getY() + 40.0f, 1.0f, sec.getHeight() - 52.0f));
        }
        const bool lit = i == 1 ? lastSectionStates[1] : lastSectionStates[0];
        g.setFont (displayFont (16.0f));
        g.setColour (lit ? Colours::accent : Colours::textDim);
        g.drawText (sectionNames[i], juce::Rectangle<float> (sec.getX() + 16.0f, sec.getY() + 36.0f, 120.0f, 22.0f),
                    juce::Justification::centredLeft, false);
    }
    drawSectionTitle (g, titleRow (swarmArea),   "SWARM",   lastSectionStates[2]);
    drawSectionTitle (g, titleRow (fuzzArea),    "SMOKE",   lastSectionStates[3]);
    drawSectionTitle (g, titleRow (flowArea),    "WINGS",   lastSectionStates[4]);

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
    chainStrip.refresh();
    eqPage.tick();
    reverbPage.tick();

    const int learning = processor.getMidiLearnTarget();
    int t = 0;
    for (auto* f : { &oct1Switch, &oct2Switch, &magicSwitch, &bypassSwitch })
        f->setLearning (learning == t++);

    const bool synced = paramOn (ParamIDs::flowSync);
    flowSpeedKnob.setVisible (! synced);
    flowDivKnob.setVisible (synced);
    const bool hiveSynced = paramOn (ParamIDs::rbSync);
    rbTimeKnob.setVisible (! hiveSynced);
    rbDivKnob.setVisible (hiveSynced);

    const bool voicesOn = paramOn (ParamIDs::rbOn) || venom;
    const std::array<bool, 5> states { noiseOn || voicesOn, voicesOn, paramOn (ParamIDs::swarmOn),
                                       paramOn (ParamIDs::fuzzOn), paramOn (ParamIDs::flowOn) };

    setSectionDimmed ({ &snapToggle, &followToggle, &rbSyncToggle, &pitchKnob, &primaryKnob, &secondaryKnob, &toneKnob, &trackingKnob,
                        &magicKnob, &rbTimeKnob, &rbDivKnob, &patternSelector }, ! states[1]);
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
