#include "Pages.h"

using namespace Theme;

namespace
{
    constexpr double kMinHz = 20.0, kMaxHz = 20000.0;

    void drawPanelTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const juce::String& subtitle, bool active)
    {
        auto row = area.reduced (16.0f, 0.0f).withTrimmedTop (8.0f).withHeight (26.0f);
        drawSectionTitle (g, row, title, active);
        g.setFont (font (12.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText (subtitle, row.withTrimmedLeft (22.0f + (float) juce::GlyphArrangement::getStringWidthInt (displayFont (23.0f), title) + 12.0f),
                    juce::Justification::centredLeft, false);
    }

    juce::String hzLabel (double f)
    {
        return f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f);
    }
}

//==============================================================================
EqGraph::EqGraph (juce::AudioProcessorValueTreeState& s, float range) : state (s), dbRange (range)
{
}

void EqGraph::setAnalyser (SpectrumTap* t, std::function<double()> sr)
{
    tap = t;
    getSampleRate = std::move (sr);
}

void EqGraph::setSelectedNode (int index)
{
    if (selected != index)
    {
        selected = index;
        repaint();
    }
}

float EqGraph::xForFreq (double f) const noexcept
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * (float) (std::log (juce::jlimit (kMinHz, kMaxHz, f) / kMinHz) / std::log (kMaxHz / kMinHz));
}

double EqGraph::freqForX (float x) const noexcept
{
    const auto r = plotArea();
    return kMinHz * std::pow (kMaxHz / kMinHz, (double) ((x - r.getX()) / r.getWidth()));
}

float EqGraph::yForDb (float db) const noexcept
{
    const auto r = plotArea();
    return r.getCentreY() - db / dbRange * (r.getHeight() * 0.5f - 8.0f);
}

float EqGraph::dbForY (float y) const noexcept
{
    const auto r = plotArea();
    return (r.getCentreY() - y) / (r.getHeight() * 0.5f - 8.0f) * dbRange;
}

float EqGraph::paramValue (const char* id) const
{
    return state.getRawParameterValue (id)->load();
}

void EqGraph::setParamValue (const char* id, float value)
{
    if (auto* p = state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

juce::Point<float> EqGraph::nodePosition (int index) const
{
    const auto& n = nodes[(size_t) index];
    const double f = paramValue (n.freqId);
    const auto r = plotArea();
    const float x = juce::jlimit (r.getX() + 11.0f, r.getRight() - 11.0f, xForFreq (f));
    const float db = n.gainId != nullptr ? paramValue (n.gainId) : (response != nullptr ? response (f) : 0.0f);
    return { x, juce::jlimit (plotArea().getY() + 8.0f, plotArea().getBottom() - 8.0f, yForDb (db)) };
}

int EqGraph::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const float d = nodePosition (i).getDistanceFrom (p);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void EqGraph::tick()
{
    if (! isShowing())
    {
        if (tap != nullptr)
            tap->setActive (false);
        return;
    }

    bool dirty = false;

    // Analyser: pull the output, FFT the latest window
    if (tap != nullptr)
    {
        tap->setActive (true);
        int pulled = 0;
        for (int n; (n = tap->pull (pullBuffer.data(), (int) pullBuffer.size())) > 0;)
        {
            pulled += n;
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) ringPos] = pullBuffer[(size_t) i];
                ringPos = (ringPos + 1) % fftSize;
            }
        }

        if (pulled > 0)
        {
            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)];
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            const float norm = 4.0f / (float) fftSize;   // Hann coherent gain 0.5, one-sided
            for (size_t i = 0; i < spectrum.size(); ++i)
            {
                const float db = juce::Decibels::gainToDecibels (fftData[i] * norm, -120.0f);
                spectrum[i] = juce::jmax (db, spectrum[i] - 1.6f);   // fast attack, slow fall
            }
            haveSpectrum = true;
            dirty = true;
        }
        else if (haveSpectrum)
        {
            for (auto& v : spectrum)
                v -= 1.6f;
            dirty = true;
        }
    }

    // Curve changes (probe the response - cheap and catches every parameter)
    std::vector<float> probe;
    probe.reserve (40);
    for (int i = 0; i < 32; ++i)
        probe.push_back (response != nullptr ? response (kMinHz * std::pow (kMaxHz / kMinHz, i / 31.0)) : 0.0f);
    for (const auto& n : nodes)
        probe.push_back (paramValue (n.freqId));
    if (probe != lastParamValues)
    {
        lastParamValues = std::move (probe);
        dirty = true;
    }

    if (dirty)
        repaint();
}

void EqGraph::paint (juce::Graphics& g)
{
    const auto r = plotArea();

    // Grid (cached)
    if (grid.isNull() || grid.getWidth() != getWidth() || grid.getHeight() != getHeight())
    {
        grid = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics gg (grid);
        gg.setColour (Colours::inset);
        gg.fillRoundedRectangle (r, 6.0f);
        drawHoneycomb (gg, r, 11.0f, Colours::accent.withAlpha (0.03f), 0.8f);

        gg.setFont (font (11.5f, true));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xForFreq (f);
            gg.setColour (Colours::text.withAlpha (0.07f));
            gg.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
            gg.setColour (Colours::textFaint);
            gg.drawText (hzLabel (f), juce::Rectangle<float> (x + 3.0f, r.getBottom() - 16.0f, 30.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        const float step = 6.0f;
        const float top = std::floor (dbRange / step) * step;
        for (float db = -top; db <= top + 0.01f; db += step)
        {
            const float y = yForDb (db);
            gg.setColour (std::abs (db) < 0.01f ? Colours::text.withAlpha (0.16f) : Colours::text.withAlpha (0.06f));
            gg.drawHorizontalLine ((int) y, r.getX() + 2.0f, r.getRight() - 2.0f);
            if (std::abs (db) > 0.01f)
            {
                gg.setColour (Colours::textFaint);
                gg.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 4.0f, y - 7.0f, 28.0f, 14.0f),
                             juce::Justification::centredLeft, false);
            }
        }
        gg.setColour (Colours::panelBorder);
        gg.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
    g.drawImageAt (grid, 0, 0);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.reduced (1.0f).toNearestInt());

    // Analyser
    if (tap != nullptr && haveSpectrum && getSampleRate != nullptr)
    {
        const double sr = getSampleRate();
        juce::Path spec;
        const float floorY = r.getBottom();
        spec.startNewSubPath (r.getX(), floorY);
        for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
        {
            const double bin = freqForX (x) * fftSize / sr;
            const int b0 = juce::jlimit (1, (int) spectrum.size() - 2, (int) bin);
            const float frac = (float) (bin - b0);
            const float db = spectrum[(size_t) b0] + frac * (spectrum[(size_t) b0 + 1] - spectrum[(size_t) b0]);
            // -90..0 dBFS over the height, a gentle tilt (+3 dB/oct) so guitar reads flat
            const float tilted = db + 3.0f * (float) std::log2 (juce::jmax (1.0, freqForX (x) / 1000.0));
            spec.lineTo (x, juce::jmap (juce::jlimit (-90.0f, 0.0f, tilted), -90.0f, 0.0f, floorY, r.getY() + 6.0f));
        }
        spec.lineTo (r.getRight(), floorY);
        spec.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.20f), 0.0f, r.getY(),
                                                 Colours::accentDeep.withAlpha (0.03f), 0.0f, r.getBottom(), false));
        g.fillPath (spec);
        g.setColour (Colours::accent.withAlpha (0.25f));
        g.strokePath (spec, juce::PathStrokeType (1.0f));
    }

    if (response == nullptr)
        return;

    auto curvePath = [&] (auto fn)
    {
        juce::Path p;
        bool first = true;
        for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
        {
            const float y = juce::jlimit (r.getY() - 20.0f, r.getBottom() + 20.0f, yForDb (fn (freqForX (x))));
            if (first) p.startNewSubPath (x, y); else p.lineTo (x, y);
            first = false;
        }
        return p;
    };

    // Selected band on its own
    if (nodeResponse != nullptr && selected >= 0)
    {
        auto band = curvePath ([this] (double f) { return nodeResponse (selected, f); });
        juce::Path fill (band);
        fill.lineTo (r.getRight(), yForDb (0.0f));
        fill.lineTo (r.getX(), yForDb (0.0f));
        fill.closeSubPath();
        g.setColour (Colours::accentBright.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (Colours::accentBright.withAlpha (0.35f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
    }

    // The whole curve
    auto curve = curvePath (response);
    g.setColour (Colours::accent.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (honeyGradient (r));
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Nodes
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto pos = nodePosition (i);
        const auto hex = juce::Rectangle<float> (20.0f, 18.0f).withCentre (pos);
        const bool sel = i == selected, hov = i == hovered || i == dragging;
        if (sel)
        {
            g.setColour (Colours::accent.withAlpha (0.35f));
            g.fillPath (hexagon (hex.expanded (4.0f)));
            g.setGradientFill (honeyGradient (hex));
        }
        else
        {
            g.setColour (Colours::inset.withAlpha (0.9f));
        }
        g.fillPath (hexagon (hex));
        g.setColour (sel ? Colours::accentBright : (hov ? Colours::accent : Colours::textDim));
        g.strokePath (hexagon (hex), juce::PathStrokeType (1.3f));
        g.setFont (font (11.0f, true));
        g.setColour (sel ? Colours::background : Colours::text);
        g.drawText (nodes[(size_t) i].label, hex, juce::Justification::centred, false);
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging < 0)
        return;
    setSelectedNode (dragging);
    if (onNodeSelected != nullptr)
        onNodeSelected (dragging);
    const auto& n = nodes[(size_t) dragging];
    for (auto* id : { n.freqId, n.gainId })
        if (id != nullptr)
            if (auto* p = state.getParameter (id))
                p->beginChangeGesture();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto& n = nodes[(size_t) dragging];
    const bool fine = e.mods.isShiftDown();
    auto pos = e.position;
    if (fine)
        pos = e.mouseDownPosition + (e.position - e.mouseDownPosition) * 0.2f;
    setParamValue (n.freqId, (float) freqForX (pos.x));
    if (n.gainId != nullptr)
        setParamValue (n.gainId, juce::jlimit (-dbRange, dbRange, dbForY (pos.y)));
    repaint();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    const auto& n = nodes[(size_t) dragging];
    for (auto* id : { n.freqId, n.gainId })
        if (id != nullptr)
            if (auto* p = state.getParameter (id))
                p->endChangeGesture();
    dragging = -1;
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = nodeAt (e.position);
    if (i < 0)
        return;
    const auto& n = nodes[(size_t) i];
    // gain nodes: back to flat; cut nodes: switched off
    const char* id = n.gainId != nullptr ? n.gainId : n.freqId;
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (n.gainId != nullptr ? p->convertTo0to1 (0.0f) : p->getDefaultValue());
        p->endChangeGesture();
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = nodeAt (e.position);
    if (i < 0 || nodes[(size_t) i].qId == nullptr)
        return;
    const auto* id = nodes[(size_t) i].qId;
    if (auto* p = state.getParameter (id))
    {
        const float q = paramValue (id) * std::pow (2.0f, w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 1.5f);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (q));
        p->endChangeGesture();
    }
}

//==============================================================================
EqPage::EqPage (SwarmnessAudioProcessor& p)
    : processor (p), state (p.getAPVTS()),
      combGraph (p.getAPVTS(), 13.0f), carveGraph (p.getAPVTS(), 19.0f)
{
    using namespace ParamIDs;
    setBufferedToImage (true);

    auto attachPower = [this] (PowerButton& b, const char* id, const juce::String& tip)
    {
        addAndMakeVisible (b);
        b.setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, b));
    };
    attachPower (combPower, geqOn, "COMB graphic EQ on/off");
    attachPower (carvePower, peqOn, "CARVE parametric EQ on/off");

    // COMB
    combGraph.response = [this] (double f)
    {
        std::array<float, swarm::GraphicEq::numBands> gains {};
        for (size_t b = 0; b < gains.size(); ++b)
            gains[b] = state.getRawParameterValue (geqBands[b])->load();
        return swarm::GraphicEq::responseDb (gains, state.getRawParameterValue (geqLevel)->load(), 48000.0, f);
    };
    combGraph.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (combGraph);

    static const char* combCaptions[] { "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    for (int b = 0; b <= swarm::GraphicEq::numBands; ++b)
    {
        const bool level = b == swarm::GraphicEq::numBands;
        auto f = std::make_unique<Fader> (level ? juce::String ("LEVEL") : juce::String (combCaptions[b]), true);
        f->setCompact (true);
        f->attach (state, level ? geqLevel : geqBands[b],
                   level ? juce::String ("LEVEL: output of the COMB EQ, +-12 dB")
                         : "COMB " + juce::String (combCaptions[b]) + " Hz band, +-12 dB (double-click = 0)");
        f->getSlider().setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (*f);
        combFaders.push_back (std::move (f));
    }

    // CARVE
    carveGraph.nodes = { { "LC", peqHpFreq, nullptr, nullptr },
                         { "L",  peqLowFreq, peqLowGain, nullptr },
                         { "1",  peqB1Freq, peqB1Gain, peqB1Q },
                         { "2",  peqB2Freq, peqB2Gain, peqB2Q },
                         { "3",  peqB3Freq, peqB3Gain, peqB3Q },
                         { "H",  peqHighFreq, peqHighGain, nullptr },
                         { "HC", peqLpFreq, nullptr, nullptr } };
    auto settingsNow = [this]
    {
        auto v = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
        swarm::ParametricEq::Settings s;
        s.hpHz = v (peqHpFreq);   s.lpHz = v (peqLpFreq);
        s.lowHz = v (peqLowFreq); s.lowDb = v (peqLowGain);
        s.highHz = v (peqHighFreq); s.highDb = v (peqHighGain);
        s.bellHz = { v (peqB1Freq), v (peqB2Freq), v (peqB3Freq) };
        s.bellDb = { v (peqB1Gain), v (peqB2Gain), v (peqB3Gain) };
        s.bellQ  = { v (peqB1Q), v (peqB2Q), v (peqB3Q) };
        return s;
    };
    carveGraph.response = [settingsNow] (double f) { return swarm::ParametricEq::responseDb (settingsNow(), 48000.0, f); };
    carveGraph.nodeResponse = [settingsNow] (int node, double f) { return swarm::ParametricEq::bandResponseDb (settingsNow(), node, 48000.0, f); };
    carveGraph.setAnalyser (&p.getSpectrumTap(), [&p] { return p.getCurrentSampleRate(); });
    carveGraph.onNodeSelected = [this] (int node)
    {
        if (node >= 1 && node <= 5)
            selectBand (node - 1);
    };
    carveGraph.setTooltip ("Drag the nodes (SHIFT = fine), mouse wheel on a bell = Q, double-click = reset. "
                           "LC / HC = 24 dB/oct low / high cut. Behind the curve: the plug-in's output spectrum");
    addAndMakeVisible (carveGraph);

    bandSelector.onSelect = [this] (int i) { selectBand (i); };
    bandSelector.setTooltip ("Which band the knobs edit (or click a node in the graph)");
    addAndMakeVisible (bandSelector);

    lowCutKnob .attach (state, peqHpFreq, "LOW CUT: 24 dB/oct high-pass - tighten the low end before (or after) the fuzz. Fully left = off");
    highCutKnob.attach (state, peqLpFreq, "HIGH CUT: 24 dB/oct low-pass - tame fizz. Fully right = off");
    lowFreqKnob .attach (state, peqLowFreq,  "Low shelf frequency");
    lowGainKnob .attach (state, peqLowGain,  "Low shelf gain");
    highFreqKnob.attach (state, peqHighFreq, "High shelf frequency");
    highGainKnob.attach (state, peqHighGain, "High shelf gain");
    const char* bf[] { peqB1Freq, peqB2Freq, peqB3Freq }, *bg[] { peqB1Gain, peqB2Gain, peqB3Gain }, *bq[] { peqB1Q, peqB2Q, peqB3Q };
    for (size_t i = 0; i < 3; ++i)
    {
        const auto n = juce::String ((int) i + 1);
        bellFreqKnobs[i].attach (state, bf[i], "Bell " + n + " frequency");
        bellGainKnobs[i].attach (state, bg[i], "Bell " + n + " gain");
        bellQKnobs[i]   .attach (state, bq[i], "Bell " + n + " Q (width): low = broad, high = narrow notch / peak");
    }
    for (auto* k : { &lowCutKnob, &highCutKnob })
        addAndMakeVisible (k);
    for (auto* k : { &lowFreqKnob, &lowGainKnob, &highFreqKnob, &highGainKnob })
        addChildComponent (k);
    for (size_t i = 0; i < 3; ++i)
        for (auto* k : { &bellFreqKnobs[i], &bellGainKnobs[i], &bellQKnobs[i] })
            addChildComponent (k);

    selectBand (1);
}

void EqPage::selectBand (int band)
{
    selectedBand = juce::jlimit (0, 4, band);
    bandSelector.setSelectedIndex (selectedBand);
    carveGraph.setSelectedNode (selectedBand + 1);

    lowFreqKnob.setVisible (selectedBand == 0);
    lowGainKnob.setVisible (selectedBand == 0);
    highFreqKnob.setVisible (selectedBand == 4);
    highGainKnob.setVisible (selectedBand == 4);
    for (int i = 0; i < 3; ++i)
    {
        const bool v = selectedBand == i + 1;
        bellFreqKnobs[(size_t) i].setVisible (v);
        bellGainKnobs[(size_t) i].setVisible (v);
        bellQKnobs[(size_t) i].setVisible (v);
    }
}

void EqPage::resized()
{
    const float h = (float) getHeight();
    combArea  = { 0.0f, 0.0f, 500.0f, h };
    carveArea = { 512.0f, 0.0f, (float) getWidth() - 512.0f, h };

    auto powerFor = [] (juce::Rectangle<float> a) { return juce::Rectangle<int> ((int) a.getRight() - 40, (int) a.getY() + 8, 26, 26); };
    combPower.setBounds (powerFor (combArea));
    carvePower.setBounds (powerFor (carveArea));

    combGraph.setBounds (16, 44, 468, 118);
    {
        const int fw = 42, x0 = 16 + (468 - fw * (int) combFaders.size()) / 2;
        for (size_t i = 0; i < combFaders.size(); ++i)
            combFaders[i]->setBounds (x0 + fw * (int) i, 174, fw, 276);
    }

    const int cx = (int) carveArea.getX() + 16, cw = (int) carveArea.getWidth() - 32;
    carveGraph.setBounds (cx, 44, cw, 262);
    lowCutKnob .setBounds (cx,      336, 76, 104);
    highCutKnob.setBounds (cx + 78, 336, 76, 104);
    const int bx = cx + 186;
    bandSelector.setBounds (bx, 318, cw - 186, 24);
    auto place = [bx] (std::initializer_list<Knob*> knobs)
    {
        int x = bx + 10;
        for (auto* k : knobs)
        {
            k->setBounds (x, 346, 80, 104);
            x += 104;
        }
    };
    place ({ &lowFreqKnob, &lowGainKnob });
    place ({ &highFreqKnob, &highGainKnob });
    for (size_t i = 0; i < 3; ++i)
        place ({ &bellFreqKnobs[i], &bellGainKnobs[i], &bellQKnobs[i] });
}

void EqPage::paint (juce::Graphics& g)
{
    drawPanel (g, combArea);
    drawPanel (g, carveArea);
    drawPanelTitle (g, combArea, "COMB", "10-band graphic EQ", combOn);
    drawPanelTitle (g, carveArea, "CARVE", "parametric EQ", carveOn);

    // divider between the cuts and the band knobs
    const float dx = carveArea.getX() + 16.0f + 172.0f;
    g.setColour (Colours::panelBorder);
    g.drawVerticalLine ((int) dx, 322.0f, 444.0f);
    g.setFont (font (12.0f, true));
    g.setColour (Colours::textFaint);
    g.drawText ("CUTS  24 dB/oct", juce::Rectangle<float> (carveArea.getX() + 16.0f, 316.0f, 156.0f, 16.0f), juce::Justification::centred, false);
}

void EqPage::tick()
{
    const bool c = combPower.getToggleState(), v = carvePower.getToggleState();
    if (c != combOn || v != carveOn)
    {
        combOn = c;
        carveOn = v;
        repaint();
    }
    for (auto& f : combFaders)
        f->setAlpha (combOn ? 1.0f : 0.45f);
    combGraph.setAlpha (combOn ? 1.0f : 0.5f);
    combGraph.tick();
    carveGraph.tick();
}

//==============================================================================
TailView::TailView (SwarmnessAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (false, false);
}

void TailView::tick (float wetLevel)
{
    history[(size_t) historyPos] = wetLevel;
    historyPos = (historyPos + 1) % (int) history.size();
    glow = juce::jmax (wetLevel, glow * 0.9f);

    repaint();
}

void TailView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colours::inset);
    g.fillRoundedRectangle (r, 6.0f);
    drawHoneycomb (g, r, 12.0f, Colours::accent.withAlpha (0.03f), 0.8f);

    auto& s = processor.getAPVTS();
    auto v = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const int type = (int) v (ParamIDs::revType);
    const bool irMode = type == ReverbStage::impulse;
    const float pd = v (ParamIDs::revPreDelay) * 0.001f;
    const float decay = v (ParamIDs::revDecay);
    const auto envelope = irMode ? processor.getReverbIREnvelope() : std::vector<float>();
    const double irSeconds = processor.getReverbIRSeconds();

    const float total = irMode ? (float) juce::jmax (0.3, irSeconds + pd) : juce::jmax (0.6f, pd + decay * 1.05f);
    auto plot = r.reduced (12.0f, 14.0f).withTrimmedBottom (24.0f);
    const float mid = plot.getCentreY(), halfH = plot.getHeight() * 0.5f;

    // time grid
    g.setFont (font (11.5f, true));
    const float stepS = total > 8.0f ? 2.0f : (total > 3.0f ? 1.0f : (total > 1.2f ? 0.5f : 0.1f));
    for (float t = 0.0f; t <= total; t += stepS)
    {
        const float x = plot.getX() + plot.getWidth() * t / total;
        g.setColour (Colours::text.withAlpha (0.06f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (Colours::textFaint);
        g.drawText (juce::String (t, stepS < 0.5f ? 1 : (stepS < 1.0f ? 1 : 0)) + " s", juce::Rectangle<float> (x + 3.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (Colours::text.withAlpha (0.1f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

    if (irMode && envelope.empty())
    {
        g.setFont (displayFont (22.0f));
        g.setColour (Colours::textDim);
        g.drawText ("load an impulse response", plot, juce::Justification::centred, false);
        return;
    }

    // amplitude envelope at time t (linear 0..1)
    const float rise = 0.012f + 0.05f * v (ParamIDs::revSize) * 0.01f;
    auto env = [&] (float t) -> float
    {
        if (t < pd)
            return 0.0f;
        const float tt = t - pd;
        // shown on a dB scale (-60..0 dB) so the decay reads as a straight slope
        auto toDisplay = [] (float gain) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, -100.0f) + 60.0f) / 60.0f); };
        if (irMode)
        {
            const float pos = (float) (tt / irSeconds) * (float) (envelope.size() - 1);
            const int i = (int) pos;
            if (i >= (int) envelope.size() - 1)
                return 0.0f;
            return toDisplay (envelope[(size_t) i] + (pos - (float) i) * (envelope[(size_t) i + 1] - envelope[(size_t) i]));
        }
        const float build = juce::jmin (1.0f, tt / rise);
        return build * toDisplay (std::pow (10.0f, -3.0f * tt / decay));
    };

    // "impulse response" hairlines under the envelope
    juce::Random rng (1234 + type);
    juce::Path hair;
    for (float x = plot.getX(); x < plot.getRight(); x += 1.6f)
    {
        const float t = (x - plot.getX()) / plot.getWidth() * total;
        const float a = env (t) * (0.35f + 0.65f * rng.nextFloat());
        if (a > 0.002f)
        {
            hair.startNewSubPath (x, mid - a * halfH);
            hair.lineTo (x, mid + a * halfH * 0.85f);
        }
    }
    g.setColour (Colours::accent.withAlpha (0.35f + 0.5f * juce::jmin (1.0f, glow * 6.0f)));
    g.strokePath (hair, juce::PathStrokeType (1.0f));

    // envelope outline
    juce::Path outline;
    bool first = true;
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        const float t = (x - plot.getX()) / plot.getWidth() * total;
        const float y = mid - env (t) * halfH;
        if (first) outline.startNewSubPath (x, y); else outline.lineTo (x, y);
        first = false;
    }
    g.setColour (Colours::accent.withAlpha (0.3f));
    g.strokePath (outline, juce::PathStrokeType (4.0f));
    g.setGradientFill (honeyGradient (plot));
    g.strokePath (outline, juce::PathStrokeType (1.6f));

    // pre-delay marker
    if (pd > 0.002f)
    {
        const float x = plot.getX() + plot.getWidth() * pd / total;
        g.setColour (Colours::textDim);
        g.drawVerticalLine ((int) x, plot.getY(), mid);
        g.setFont (font (11.5f, true));
        g.drawText ("PRE", juce::Rectangle<float> (x + 3.0f, plot.getY(), 40.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    // live wet level strip at the bottom
    auto strip = r.reduced (12.0f, 0.0f).withTop (r.getBottom() - 10.0f).withHeight (4.0f);
    const float bw = strip.getWidth() / (float) history.size();
    for (int i = 0; i < (int) history.size(); ++i)
    {
        const float lvl = history[(size_t) ((historyPos + i) % (int) history.size())];
        const float a = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lvl, -80.0f) + 60.0f) / 60.0f);
        if (a > 0.0f)
        {
            g.setColour (Colours::accentBright.withAlpha (0.15f + 0.75f * a));
            g.fillRect (strip.getX() + bw * (float) i, strip.getY(), bw - 0.5f, strip.getHeight());
        }
    }
}

//==============================================================================
ReverbPage::ReverbPage (SwarmnessAudioProcessor& p)
    : processor (p), state (p.getAPVTS()),
      typeSelector (*p.getAPVTS().getParameter (ParamIDs::revType), { "ROOM", "PLATE", "HALL", "ABYSS", "IR" }),
      tail (p)
{
    using namespace ParamIDs;
    setBufferedToImage (true);

    addAndMakeVisible (power);
    power.setTooltip ("CRYPT reverb on/off. Switching it off lets the tail ring out");
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, revOn, power);

    typeSelector.setTooltip ("ROOM = tight and close, PLATE = dense and bright, HALL = big, ABYSS = huge, dark, moving. "
                             "IR = your impulse response (load it on the right)");
    addAndMakeVisible (typeSelector);
    addAndMakeVisible (tail);

    mixKnob     .attach (state, revMix,      "MIX: dry / reverb. 50% = both at full level, 100% = reverb only");
    decayKnob   .attach (state, revDecay,    "DECAY: reverb time (RT60) - how long the tail takes to fall by 60 dB");
    sizeKnob    .attach (state, revSize,     "SIZE: the room - small and dense to vast and sparse");
    preDelayKnob.attach (state, revPreDelay, "PRE-DELAY: gap before the reverb starts - keeps the attack clear");
    toneKnob    .attach (state, revTone,     "TONE: dark <-> bright (the highs also die faster when dark)");
    lowCutKnob  .attach (state, revLowCut,   "LOW CUT: removes lows from the reverb only - keeps heavy riffs from turning to mud");
    modKnob     .attach (state, revMod,      "MOD: slow movement in the tail - lush, and no metallic ringing");
    duckKnob    .attach (state, revDuck,     "DUCK: the reverb dips while you play and blooms in the gaps - big space without the mud");
    for (auto* k : { &mixKnob, &decayKnob, &sizeKnob, &preDelayKnob, &toneKnob, &lowCutKnob, &modKnob, &duckKnob })
        addAndMakeVisible (k);

    loadButton.setTooltip ("Load an impulse response (WAV / AIFF / FLAC, up to 12 s). Or drop a file on this page");
    loadButton.onClick = [this] { chooseFile(); };
    clearButton.setTooltip ("Forget the impulse response");
    clearButton.onClick = [this] { processor.clearReverbIR(); message.clear(); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
}

void ReverbPage::resized()
{
    panelArea = getLocalBounds().toFloat();
    power.setBounds ((int) panelArea.getRight() - 40, 8, 26, 26);
    typeSelector.setBounds ((int) panelArea.getRight() - 52 - 400, 10, 400, 24);

    tail.setBounds (16, 46, 700, 244);
    irArea = { 728.0f, 46.0f, panelArea.getWidth() - 744.0f, 244.0f };
    loadButton .setBounds ((int) irArea.getX() + 16, (int) irArea.getBottom() - 46, 150, 30);
    clearButton.setBounds (loadButton.getRight() + 10, loadButton.getY(), 90, 30);

    const int slots = 8, y = 312;
    const float slotW = (panelArea.getWidth() - 32.0f) / (float) slots;
    int i = 0;
    for (auto* k : { &mixKnob, &decayKnob, &sizeKnob, &preDelayKnob, &toneKnob, &lowCutKnob, &modKnob, &duckKnob })
    {
        const int cx = (int) (16.0f + slotW * ((float) i++ + 0.5f));
        k->setBounds (cx - 50, y, 100, 110);
    }
}

void ReverbPage::paint (juce::Graphics& g)
{
    drawPanel (g, panelArea);
    drawPanelTitle (g, panelArea, "CRYPT", "reverb", on);

    // IR box
    const auto box = irArea;
    g.setColour (Colours::inset);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (dragHover ? Colours::accentBright : (irMode ? Colours::accent.withAlpha (0.6f) : Colours::panelBorder));
    g.drawRoundedRectangle (box, 6.0f, dragHover ? 2.0f : 1.0f);

    auto t = box.reduced (16.0f, 12.0f);
    g.setFont (displayFont (19.0f));
    g.setGradientFill (honeyGradient (t.withHeight (24.0f)));
    g.drawText ("IMPULSE RESPONSE", t.removeFromTop (24.0f), juce::Justification::centredLeft, false);
    t.removeFromTop (8.0f);

    g.setFont (font (15.0f, true));
    g.setColour (irDescription.isNotEmpty() ? Colours::text : Colours::textDim);
    g.drawFittedText (irDescription.isNotEmpty() ? irDescription : juce::String ("none loaded"), t.removeFromTop (40.0f).toNearestInt(),
                      juce::Justification::topLeft, 2, 1.0f);

    g.setFont (font (13.0f));
    if (message.isNotEmpty())
    {
        g.setColour (Colours::ledRed);
        g.drawFittedText (message, t.removeFromTop (54.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }
    else
    {
        g.setColour (Colours::textFaint);
        juce::String hint = irDescription.isEmpty() ? "Load a WAV / AIFF / FLAC (halls, plates, cathedrals, spring captures...) or drop it here."
                          : (irMode ? "Playing through this IR. PRE-DELAY, TONE, LOW CUT, DUCK and MIX still work."
                                    : "Select IR above to hear it.");
        g.drawFittedText (hint, t.removeFromTop (54.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }
}

void ReverbPage::tick()
{
    const bool nowOn = power.getToggleState();
    const bool nowIr = (int) state.getRawParameterValue (ParamIDs::revType)->load() == ReverbStage::impulse;
    const auto desc = processor.getReverbIRDescription();
    if (messageTicks > 0 && --messageTicks == 0)
    {
        message.clear();
        repaint();
    }
    if (nowOn != on || nowIr != irMode || desc != irDescription)
    {
        on = nowOn;
        irMode = nowIr;
        irDescription = desc;
        repaint();
    }

    for (auto* k : { &decayKnob, &sizeKnob, &modKnob })
        k->setAlpha (irMode ? 0.35f : 1.0f);
    for (auto* k : { &mixKnob, &preDelayKnob, &toneKnob, &lowCutKnob, &duckKnob })
        k->setAlpha (on ? 1.0f : 0.6f);
    clearButton.setEnabled (irDescription.isNotEmpty());

    if (isShowing())
        tail.tick (processor.getMeters().reverbLevel.exchange (0.0f));
}

bool ReverbPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg"))
            return true;
    return false;
}

void ReverbPage::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg"))
        {
            load (juce::File (f));
            break;
        }
    repaint();
}

void ReverbPage::load (const juce::File& file)
{
    message = processor.loadReverbIR (file);
    messageTicks = message.isNotEmpty() ? 150 : 0;
    if (message.isEmpty())
        if (auto* type = state.getParameter (ParamIDs::revType))
        {
            // loading an IR means you want to hear it
            type->beginChangeGesture();
            type->setValueNotifyingHost (type->convertTo0to1 ((float) ReverbStage::impulse));
            type->endChangeGesture();
        }
    repaint();
}

void ReverbPage::chooseFile()
{
    const auto current = processor.getReverbIRFile();
    chooser = std::make_unique<juce::FileChooser> ("Load an impulse response",
                                                   current.existsAsFile() ? current.getParentDirectory()
                                                                          : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<ReverbPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->load (fc.getResult());
                          });
}
