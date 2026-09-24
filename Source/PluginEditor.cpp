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
      octaveSelector (param (state, ParamIDs::octave), { "-2", "-1", "0", "+1", "+2" }),
      qualitySelector (param (state, ParamIDs::quality), { "LIVE", "STUDIO" }),
      footswitch (param (state, ParamIDs::bypass))
{
    using namespace ParamIDs;

    logo = juce::ImageCache::getFromMemory (BinaryData::header_logo_png, BinaryData::header_logo_pngSize);

    backdrop.painter = [this] (juce::Graphics& g) { paintBackdrop (g); };
    backdrop.setInterceptsMouseClicks (false, false);
    backdrop.setBufferedToImage (true);
    backdrop.setOpaque (true);
    addAndMakeVisible (backdrop);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (infoButton);
    infoButton.setTooltip ("Help");
    infoButton.onClick = [this] { infoOverlay.setVisible (true); infoOverlay.toFront (false); };

    // VOLTAGE
    attachButton (pitchPower, pitchOn, "Voltage section on/off (engaging it triggers the Rise glide)");
    addAndMakeVisible (octaveSelector);
    octaveSelector.setTooltip ("Octave shift");
    addAndMakeVisible (qualitySelector);
    qualitySelector.setTooltip ("LIVE: zero-latency engine for playing/monitoring. STUDIO: highest quality spectral engine (adds latency, compensated by the host).");

    semiKnob .attach (state, semitone,  "Additional interval in semitones (e.g. +7 = fifth)");
    riseKnob .attach (state, rise,      "Glide time into the target pitch");
    rangeKnob.attach (state, randRange, "Random pitch wander range");
    speedKnob.attach (state, randSpeed, "Random pitch wander speed");
    rushKnob .attach (state, rush,      "Organic pitch drift depth");
    angerKnob.attach (state, anger,     "Rhythmic semitone jumps (probability and size)");
    modRateKnob.attach (state, modRate, "Modulation rate for Rush and Anger");

    for (auto* c : std::initializer_list<juce::Component*> { &semiKnob, &riseKnob, &rangeKnob, &speedKnob,
                                                             &rushKnob, &angerKnob, &modRateKnob, &pitchScope })
        addAndMakeVisible (c);

    // TONE
    lowCutFader .attach (state, lowCut,  "Low cut (24 dB/oct) on the effect signal");
    highCutFader.attach (state, highCut, "High cut (12 dB/oct) on the effect signal");
    midFader    .attach (state, mid,     "Mid boost around 850 Hz");

    // OUTPUT
    mixFader   .attach (state, mix,    "Dry / effect blend (equal power, latency aligned)");
    driveFader .attach (state, drive,  "Tube-style drive, 4x oversampled");
    volumeFader.attach (state, output, "Output level");

    for (auto* c : std::initializer_list<juce::Component*> { &lowCutFader, &highCutFader, &midFader,
                                                             &mixFader, &driveFader, &volumeFader })
        addAndMakeVisible (c);

    // SWARM
    attachButton (swarmPower, swarmOn,   "Swarm section on/off");
    attachButton (deepToggle, swarmDeep, "Deep mode: 8 voices with feedback");
    swarmDepthKnob.attach (state, swarmDepth, "Modulation depth");
    swarmRateKnob .attach (state, swarmRate,  "Modulation rate");
    swarmMixKnob  .attach (state, swarmMix,   "Ensemble mix");
    addAndMakeVisible (swarmDepthKnob);
    addAndMakeVisible (swarmRateKnob);
    addAndMakeVisible (swarmMixKnob);

    // FLOW
    attachButton (flowPower,  flowOn,   "Flow section on/off");
    attachButton (hardToggle, flowHard, "Hard stutter gate (off = smooth tremolo)");
    attachButton (syncToggle, flowSync, "Sync to host tempo");
    flowAmountKnob.attach (state, flowAmount, "Gate depth");
    flowSpeedKnob .attach (state, flowSpeed,  "Gate rate (free)");
    flowDivKnob   .attach (state, flowDiv,    "Gate rate (tempo division)");
    addAndMakeVisible (flowAmountKnob);
    addAndMakeVisible (flowSpeedKnob);
    addChildComponent (flowDivKnob);

    // Footer
    addAndMakeVisible (footswitch);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

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
    infoButton.setBounds (baseWidth - 16 - 32, 16, 32, 32);

    // Section areas
    toneArea    = { 16.0f, 76.0f, 164.0f, 472.0f };
    outputArea  = { 820.0f, 76.0f, 164.0f, 472.0f };
    voltageArea = { 192.0f, 76.0f, 616.0f, 276.0f };
    swarmArea   = { 192.0f, 364.0f, 302.0f, 184.0f };
    flowArea    = { 506.0f, 364.0f, 302.0f, 184.0f };

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };

    // VOLTAGE
    pitchPower.setBounds (powerFor (voltageArea));
    const int vx = (int) voltageArea.getX();
    const int vy = (int) voltageArea.getY();
    octaveSelector .setBounds (vx + 16, vy + 70, 316, 32);
    qualitySelector.setBounds (vx + 16, vy + 132, 160, 28);

    const int knobY = vy + 170;
    semiKnob .setBounds (vx + 12,  knobY, 80, 100);
    riseKnob .setBounds (vx + 92,  knobY, 80, 100);
    rangeKnob.setBounds (vx + 172, knobY, 80, 100);
    speedKnob.setBounds (vx + 252, knobY, 80, 100);

    const int mx = vx + 364;
    pitchScope .setBounds (mx, vy + 52, 236, 108);
    rushKnob   .setBounds (mx - 2,   knobY, 80, 100);
    angerKnob  .setBounds (mx + 78,  knobY, 80, 100);
    modRateKnob.setBounds (mx + 158, knobY, 80, 100);

    // TONE / OUTPUT faders
    auto layoutFaders = [] (juce::Rectangle<float> area, std::initializer_list<Fader*> faders)
    {
        auto r = area.reduced (8.0f, 0.0f).withTrimmedTop (44.0f).withTrimmedBottom (12.0f).toNearestInt();
        const int w = r.getWidth() / (int) faders.size();
        for (auto* f : faders)
            f->setBounds (r.removeFromLeft (w));
    };
    layoutFaders (toneArea,   { &lowCutFader, &highCutFader, &midFader });
    layoutFaders (outputArea, { &mixFader, &driveFader, &volumeFader });

    // SWARM
    swarmPower.setBounds (powerFor (swarmArea));
    deepToggle.setBounds ((int) swarmArea.getRight() - 40 - 72, (int) swarmArea.getY() + 9, 64, 24);
    {
        const int y = (int) swarmArea.getY() + 60;
        const int x = (int) swarmArea.getX() + 16;
        swarmDepthKnob.setBounds (x,       y, 86, 104);
        swarmRateKnob .setBounds (x + 92,  y, 86, 104);
        swarmMixKnob  .setBounds (x + 184, y, 86, 104);
    }

    // FLOW
    flowPower.setBounds (powerFor (flowArea));
    syncToggle.setBounds ((int) flowArea.getRight() - 40 - 72,  (int) flowArea.getY() + 9, 64, 24);
    hardToggle.setBounds ((int) flowArea.getRight() - 40 - 142, (int) flowArea.getY() + 9, 64, 24);
    {
        const int y = (int) flowArea.getY() + 60;
        const int x = (int) flowArea.getX() + 40;
        flowAmountKnob.setBounds (x,       y, 96, 104);
        flowSpeedKnob .setBounds (x + 124, y, 96, 104);
        flowDivKnob   .setBounds (x + 124, y, 96, 104);
    }

    // Footer
    inMeter   .setBounds (192, 578, 212, 26);
    footswitch.setBounds (428, 564, 164, 56);   // LED + stomp + caption
    outMeter  .setBounds (604, 578, 204, 26);

    infoOverlay.setBounds (getLocalBounds());
    backdrop.setBounds (getLocalBounds());
}

//==============================================================================
void MainPanel::paintBackdrop (juce::Graphics& g)
{
    // Background with subtle vignette
    g.setGradientFill (juce::ColourGradient (Colours::backgroundHi, baseWidth * 0.5f, baseHeight * 0.35f,
                                             Colours::background, 0.0f, (float) baseHeight, true));
    g.fillAll();

    // Fine diagonal texture
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
            const float w = h * (float) logo.getWidth() / (float) logo.getHeight();
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, juce::Rectangle<float> (16.0f, 8.0f, w, h), juce::RectanglePlacement::centred);

            g.setFont (font (13.0f, true));
            g.setColour (Colours::textDim);
            g.drawText ("PITCH SWARM ENGINE", juce::Rectangle<float> (24.0f + w, 22.0f, 180.0f, 20.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // Panels
    for (auto a : { voltageArea, toneArea, outputArea, swarmArea, flowArea })
        drawPanel (g, a);

    auto titleRow = [] (juce::Rectangle<float> a) { return a.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f); };

    drawSectionTitle (g, titleRow (voltageArea), "VOLTAGE", paramOn (ParamIDs::pitchOn));
    drawSectionTitle (g, titleRow (toneArea),    "TONE",    true);
    drawSectionTitle (g, titleRow (outputArea),  "OUTPUT",  true);
    drawSectionTitle (g, titleRow (swarmArea),   "SWARM",   paramOn (ParamIDs::swarmOn));
    drawSectionTitle (g, titleRow (flowArea),    "FLOW",    paramOn (ParamIDs::flowOn));

    // VOLTAGE sub-headings and divider
    {
        const float vx = voltageArea.getX(), vy = voltageArea.getY();
        g.setFont (font (13.0f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("OCTAVE", juce::Rectangle<float> (vx + 16.0f, vy + 50.0f, 200.0f, 18.0f), juce::Justification::centredLeft, false);
        g.drawText ("ENGINE", juce::Rectangle<float> (vx + 16.0f, vy + 112.0f, 200.0f, 18.0f), juce::Justification::centredLeft, false);

        g.setFont (font (13.5f, true));
        g.setColour (Colours::textDim);
        g.drawText (latencyText, juce::Rectangle<float> (vx + 186.0f, vy + 132.0f, 150.0f, 28.0f), juce::Justification::centredLeft, false);

        g.setColour (Colours::panelBorder);
        g.drawVerticalLine ((int) (vx + 348.0f), vy + 48.0f, voltageArea.getBottom() - 14.0f);

        g.setFont (font (13.0f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("MODULATION", juce::Rectangle<float> (vx + 364.0f, vy + 32.0f, 200.0f, 18.0f), juce::Justification::centredLeft, false);
    }

    // Footer
    g.setFont (font (13.0f));
    g.setColour (Colours::textFaint);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<float> (16.0f, (float) baseHeight - 30.0f, 120.0f, 20.0f),
                juce::Justification::centredLeft, false);
}

//==============================================================================
void MainPanel::tick()
{
    auto& meters = processor.getMeters();
    inMeter .update (meters.input[0].exchange (0.0f),  meters.input[1].exchange (0.0f));
    outMeter.update (meters.output[0].exchange (0.0f), meters.output[1].exchange (0.0f));

    const bool pitchActive = paramOn (ParamIDs::pitchOn);
    pitchScope.push (meters.pitchSemitones.load(), pitchActive);

    presetBar.refresh();

    const bool synced = paramOn (ParamIDs::flowSync);
    flowSpeedKnob.setVisible (! synced);
    flowDivKnob.setVisible (synced);

    setSectionDimmed ({ &octaveSelector, &qualitySelector, &semiKnob, &riseKnob, &rangeKnob, &speedKnob,
                        &rushKnob, &angerKnob, &modRateKnob }, ! pitchActive);
    setSectionDimmed ({ &deepToggle, &swarmDepthKnob, &swarmRateKnob, &swarmMixKnob }, ! paramOn (ParamIDs::swarmOn));
    setSectionDimmed ({ &hardToggle, &syncToggle, &flowAmountKnob, &flowSpeedKnob, &flowDivKnob }, ! paramOn (ParamIDs::flowOn));

    const double sr = processor.getSampleRate();
    const auto text = sr > 0.0 ? "latency " + juce::String (processor.getLatencySamples() * 1000.0 / sr, 1) + " ms"
                               : juce::String();
    if (text != latencyText)
    {
        latencyText = text;
        backdrop.repaint();
    }

    // Section titles reflect power state
    const std::array<bool, 3> states { pitchActive, paramOn (ParamIDs::swarmOn), paramOn (ParamIDs::flowOn) };
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
