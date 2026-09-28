#include "TunerOverlay.h"

#include <algorithm>

using namespace Theme;

TunerOverlay::TunerOverlay (TunerTap& t) : tap (t)
{
    setInterceptsMouseClicks (true, true);
    muteToggle.setTooltip ("MUTE: silence the output while the tuner is open (tune on stage without the audience hearing it)");
    muteToggle.setToggleState (true, juce::dontSendNotification);
    muteToggle.onClick = [this] { tap.mute.store (muteToggle.getToggleState()); };
    closeButton.onClick = [this] { close(); };
    refDown.setTooltip ("Reference pitch down");
    refUp.setTooltip ("Reference pitch up");
    refDown.onClick = [this] { a4 = juce::jmax (430.0f, a4 - 1.0f); tap.a4.store (a4); repaint(); };
    refUp.onClick = [this] { a4 = juce::jmin (450.0f, a4 + 1.0f); tap.a4.store (a4); repaint(); };
    for (juce::Component* c : { (juce::Component*) &muteToggle, (juce::Component*) &closeButton, (juce::Component*) &refDown, (juce::Component*) &refUp })
        addAndMakeVisible (c);
    setVisible (false);
}

TunerOverlay::~TunerOverlay()
{
    tap.active.store (false);
}

void TunerOverlay::open()
{
    tap.mute.store (muteToggle.getToggleState());
    tap.active.store (true);
    a4 = juce::jlimit (430.0f, 450.0f, tap.a4.load());
    midi = -1;
    silentTicks = 999;
    setVisible (true);
    toFront (false);
    startTimerHz (30);
}

void TunerOverlay::close()
{
    stopTimer();
    tap.active.store (false);
    setVisible (false);
}

TunerOverlay::Reading TunerOverlay::read (float f, float ref)
{
    Reading r;
    if (f <= 0.0f)
        return r;
    const float note = 69.0f + 12.0f * std::log2 (f / ref);
    r.midi = juce::roundToInt (note);
    r.cents = (note - (float) r.midi) * 100.0f;
    return r;
}

juce::String TunerOverlay::noteName (int m)
{
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return m < 0 ? juce::String ("-") : juce::String (names[m % 12]);
}

void TunerOverlay::timerCallback()
{
    tap.read (window, 2048);
    const auto est = TunerEstimate::estimate (window, tap.getRate());

    // a pick attack: the pitch is sharp and wobbly for the first ~80 ms of a note - skip it
    if (est.levelDb > lastLevelDb + 6.0f)
        attackSkip = 3;
    lastLevelDb = est.levelDb;

    bool fresh = false;
    if (attackSkip > 0)
        --attackSkip;
    else if (est.hz > 0.0f && est.clarity > 0.8f)
    {
        const auto r = read (est.hz, a4);
        // a new note has to show up three times in a row before it takes over (octave slips, harmonics)
        if (r.midi != midi && midi >= 0 && silentTicks < 8)
        {
            candidateCount = r.midi == candidateMidi ? candidateCount + 1 : 1;
            candidateMidi = r.midi;
            if (candidateCount >= 3)
            {
                midi = r.midi;
                recentCount = 0;
                fresh = true;
            }
        }
        else
        {
            if (midi < 0 || r.midi != midi) recentCount = 0;
            midi = r.midi;
            candidateCount = 0;
            fresh = true;
        }
        if (fresh)
        {
            // median of the last few readings (in cents of this note)
            std::rotate (recent.begin(), recent.begin() + 1, recent.end());
            recent.back() = 1200.0f * std::log2 (est.hz / (a4 * std::pow (2.0f, (float) (midi - 69) / 12.0f)));
            recentCount = juce::jmin ((int) recent.size(), recentCount + 1);
            std::array<float, 5> sorted {};
            std::copy (recent.end() - recentCount, recent.end(), sorted.begin());
            std::sort (sorted.begin(), sorted.begin() + recentCount);
            const float cents = sorted[(size_t) (recentCount / 2)];
            hz = a4 * std::pow (2.0f, ((float) (midi - 69) + cents / 100.0f) / 12.0f);
        }
        silentTicks = 0;
    }
    else if (++silentTicks > 20)   // ~2/3 s without a clear pitch: let go
    {
        midi = -1;
        hz = 0.0f;
        recentCount = 0;
        displayedCents = 0;
    }

    // the needle: a critically damped glide, not a jump
    const float cents = midi >= 0 ? read (hz, a4).cents : 0.0f;
    const float k = 0.10f;
    velocity += k * (cents - shownCents) - 2.0f * std::sqrt (k) * velocity * 0.9f;
    shownCents = juce::jlimit (-50.0f, 50.0f, shownCents + velocity);
    // the readout moves in whole cents, with a little hysteresis
    if (std::abs (cents - (float) displayedCents) >= 1.3f)
        displayedCents = juce::roundToInt (cents);
    // IN TUNE: in under 2 cents, out over 4
    inTuneShown = midi >= 0 && (inTuneShown ? std::abs (cents) < 4.0f : std::abs (cents) < 2.0f);
    glow += ((inTuneShown ? 1.0f : 0.0f) - glow) * 0.18f;
    repaint();
}

void TunerOverlay::resized()
{
    panelArea = getLocalBounds().toFloat().withSizeKeepingCentre (660.0f, 400.0f);
    meterArea = panelArea.reduced (36.0f, 0.0f).withY (panelArea.getY() + 250.0f).withHeight (56.0f);
    const int by = (int) panelArea.getBottom() - 52;
    muteToggle.setBounds ((int) panelArea.getX() + 30, by, 90, 28);
    closeButton.setBounds ((int) panelArea.getRight() - 30 - 96, by - 2, 96, 32);
    const int cx = (int) panelArea.getCentreX();
    refDown.setBounds (cx - 110, by, 28, 28);
    refUp.setBounds (cx + 82, by, 28, 28);
}

void TunerOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panelArea.contains (e.position))
        close();
}

void TunerOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background.withAlpha (0.82f));
    drawPanel (g, panelArea, 14.0f);
    {
        auto row = panelArea.reduced (22.0f, 0.0f).withTrimmedTop (14.0f).withHeight (26.0f);
        drawSectionTitle (g, row, "TUNER", midi >= 0);
        g.setFont (font (12.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("the swarm tunes up  -  play one string", row.withTrimmedLeft (130.0f), juce::Justification::centredLeft, false);
    }

    const bool inTune = inTuneShown;
    const int cents = displayedCents;

    // the note: big, with its octave; honey when in tune
    {
        const auto area = juce::Rectangle<float> (panelArea.getX(), panelArea.getY() + 44.0f, panelArea.getWidth(), 150.0f);
        const auto name = noteName (midi);
        const auto base = name.substring (0, 1), sharp = name.substring (1);
        g.setFont (displayFont (138.0f));
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (displayFont (138.0f), base);
        const float x = area.getCentreX() - w * 0.5f;
        if (glow > 0.02f)
        {
            g.setColour (Colours::accentBright.withAlpha (0.22f * glow));
            g.fillEllipse (area.withSizeKeepingCentre (220.0f, 150.0f));
        }
        if (midi >= 0)
            g.setGradientFill (honeyGradient (area, inTune ? 1.0f : 0.85f));
        else
            g.setColour (Colours::textFaint);
        g.drawText (base, juce::Rectangle<float> (x, area.getY(), w + 4.0f, area.getHeight()), juce::Justification::centredLeft, false);
        g.setFont (displayFont (52.0f));
        g.drawText (sharp, juce::Rectangle<float> (x + w + 2.0f, area.getY() + 14.0f, 60.0f, 60.0f), juce::Justification::centredLeft, false);
        if (midi >= 0)
        {
            g.setFont (font (22.0f, true));
            g.setColour (Colours::textDim);
            g.drawText (juce::String (midi / 12 - 1), juce::Rectangle<float> (x + w + 4.0f, area.getBottom() - 58.0f, 40.0f, 30.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // cents and Hz
    {
        const auto row = juce::Rectangle<float> (panelArea.getX(), panelArea.getY() + 196.0f, panelArea.getWidth(), 40.0f);
        g.setFont (font (20.0f, true));
        g.setColour (inTune ? Colours::ledGreen : (midi >= 0 ? Colours::accent : Colours::textFaint));
        const auto text = midi < 0 ? juce::String ("no signal")
                        : inTune ? juce::String ("IN TUNE")
                                 : (cents > 0 ? "+" : "") + juce::String (cents) + " cents  " + (cents > 0 ? "(sharp)" : "(flat)");
        g.drawText (text, row.withTrimmedRight (row.getWidth() * 0.5f).withTrimmedRight (10.0f), juce::Justification::centredRight, false);
        g.setColour (Colours::textDim);
        g.drawText (midi >= 0 ? juce::String (hz, 2) + " Hz" : juce::String(), row.withTrimmedLeft (row.getWidth() * 0.5f).withTrimmedLeft (10.0f),
                    juce::Justification::centredLeft, false);
    }

    // the honeycomb meter: 21 cells, 5 cents each; the lit cell follows the string
    {
        const int cells = 21;
        const float step = meterArea.getWidth() / (float) cells;
        const float pos = midi >= 0 ? juce::jlimit (0.0f, (float) (cells - 1), (shownCents / 5.0f) + 10.0f) : -10.0f;
        for (int i = 0; i < cells; ++i)
        {
            const bool centre = i == 10;
            const float size = centre ? step * 1.25f : step * 0.92f;
            const auto r = juce::Rectangle<float> (meterArea.getX() + step * ((float) i + 0.5f) - size * 0.5f,
                                                   meterArea.getCentreY() - size * 0.55f, size, size * 1.1f);
            const auto hex = hexagon (r, true);
            const float lit = juce::jmax (0.0f, 1.0f - std::abs ((float) i - pos));
            g.setColour (Colours::inset);
            g.fillPath (hex);
            if (lit > 0.0f)
            {
                const auto c = std::abs (i - 10) <= 1 ? Colours::accentBright : Colours::accent;
                g.setColour (c.withAlpha (0.25f + 0.75f * lit));
                g.fillPath (hex);
                g.setColour (c.withAlpha (0.25f * lit));
                g.fillPath (hexagon (r.expanded (5.0f), true));
            }
            if (centre && glow > 0.02f)
            {
                g.setColour (Colours::ledGreen.withAlpha (0.6f * glow));
                g.strokePath (hex, juce::PathStrokeType (2.5f));
            }
            g.setColour (centre ? Colours::accentDeep : Colours::panelBorder);
            g.strokePath (hex, juce::PathStrokeType (1.2f));
        }
        g.setFont (font (11.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("FLAT", meterArea.withY (meterArea.getBottom() + 2.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
        g.drawText ("SHARP", meterArea.withY (meterArea.getBottom() + 2.0f).withHeight (14.0f), juce::Justification::centredRight, false);
        g.drawText ("0", meterArea.withY (meterArea.getBottom() + 2.0f).withHeight (14.0f), juce::Justification::centred, false);
    }

    // reference pitch
    {
        const auto r = juce::Rectangle<float> (panelArea.getCentreX() - 78.0f, panelArea.getBottom() - 52.0f, 156.0f, 28.0f);
        g.setFont (font (15.0f, true));
        g.setColour (Colours::textDim);
        g.drawText ("A4 = " + juce::String ((int) a4) + " Hz", r, juce::Justification::centred, false);
    }
}
