#include "Controls.h"

using namespace Theme;

namespace
{
    void configureCommonSlider (juce::Slider& s)
    {
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled (false, false, nullptr);
        // Hold Shift for fine adjustment.
        s.setVelocityModeParameters (0.35, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        s.setMouseDragSensitivity (420);   // slower, more precise drags (default 250 px for the full range)
        s.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void attachSlider (juce::Slider& slider, juce::AudioProcessorValueTreeState& state, const juce::String& id,
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
        if (auto* param = state.getParameter (id))
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }
}

//==============================================================================
Knob::Knob (const juce::String& c, bool bipolar) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    configureCommonSlider (slider);
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& tip)
{
    attachSlider (slider, state, id, attachment);
    slider.setTooltip (tip);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= getHeight() - 20 && isEnabled())
        showValueEditor();
}

void Knob::showValueEditor()
{
    valueEditor = std::make_unique<juce::TextEditor>();
    auto* ed = valueEditor.get();
    addAndMakeVisible (ed);
    ed->setBounds (getLocalBounds().removeFromBottom (20).reduced (6, 0));
    ed->setJustification (juce::Justification::centred);
    ed->setFont (font (15.0f, true));
    ed->setText (slider.getTextFromValue (slider.getValue()), false);
    ed->selectAll();
    ed->grabKeyboardFocus();

    auto finish = [this] (bool apply)
    {
        if (valueEditor == nullptr) return;
        if (apply)
        {
            const auto text = valueEditor->getText().trim();
            if (text.isNotEmpty())
                slider.setValue (slider.snapValue (slider.getValueFromText (text), juce::Slider::notDragging),
                                 juce::sendNotificationSync);
        }
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)]
        {
            if (safe != nullptr) safe->valueEditor.reset();
        });
    };
    ed->onReturnKey = [finish] { finish (true); };
    ed->onEscapeKey = [finish] { finish (false); };
    ed->onFocusLost = [finish] { finish (true); };
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (18);
    const int size = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setFont (font (15.0f, true));
    g.setColour (isEnabled() ? Colours::text.withAlpha (0.78f) : Colours::textFaint);
    g.drawText (caption, r.removeFromTop (18.0f), juce::Justification::centred, false);

    g.setFont (font (15.0f, true));
    g.setColour (isEnabled() ? Colours::accentBright : Colours::textFaint);
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromBottom (18.0f), juce::Justification::centred, false);
}

//==============================================================================
Fader::Fader (const juce::String& c, bool bipolar) : caption (c)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    configureCommonSlider (slider);
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Fader::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& tip)
{
    attachSlider (slider, state, id, attachment);
    slider.setTooltip (tip);
}

void Fader::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    r.removeFromBottom (22);
    slider.setBounds (r);
}

void Fader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setFont (font (14.5f, true));
    g.setColour (Colours::textDim);
    g.drawText (caption, r.removeFromTop (18.0f), juce::Justification::centred, false);

    g.setFont (font (15.0f, true));
    g.setColour (Colours::accentBright);
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromBottom (18.0f), juce::Justification::centred, false);
}

//==============================================================================
PowerButton::PowerButton()
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PowerButton::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState();
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto hexR = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);

    if (on)
    {
        g.setColour (Colours::accent.withAlpha (0.3f));
        g.fillPath (hexagon (hexR.expanded (2.0f), true));
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2319), c.x, hexR.getY(),
                                             juce::Colour (0xff0d0907), c.x, hexR.getBottom(), false));
    g.fillPath (hexagon (hexR.reduced (1.5f), true));
    g.setColour (on ? Colours::accent : (isMouseOver ? Colours::textDim : Colours::panelBorder.brighter (0.3f)));
    g.strokePath (hexagon (hexR.reduced (1.5f), true), juce::PathStrokeType (1.3f));

    const float ir = rad * 0.4f;
    juce::Path glyph;
    glyph.addCentredArc (c.x, c.y, ir, ir, 0.0f, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
    glyph.startNewSubPath (c.x, c.y - ir * 1.25f);
    glyph.lineTo (c.x, c.y - ir * 0.25f);
    g.setColour (on ? Colours::accentBright : Colours::textFaint);
    g.strokePath (glyph, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
PillToggle::PillToggle (const juce::String& label) : juce::ToggleButton (label)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PillToggle::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    const auto shape = hexCapsule (r);

    if (on)
    {
        g.setGradientFill (honeyGradient (r, 0.28f));
        g.fillPath (shape);
    }
    else
    {
        g.setColour (Colours::inset);
        g.fillPath (shape);
    }
    g.setColour (on ? Colours::accent.withAlpha (0.9f) : (isMouseOver ? Colours::textFaint : Colours::panelBorder.brighter (0.2f)));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    const auto led = juce::Rectangle<float> (8.0f, 7.0f).withCentre ({ r.getX() + r.getHeight() * 0.5f + 3.0f, r.getCentreY() });
    if (on)
    {
        g.setColour (Colours::accent.withAlpha (0.4f));
        g.fillPath (hexagon (led.expanded (3.0f)));
        g.setGradientFill (honeyGradient (led));
    }
    else
        g.setColour (Colours::textFaint.darker (0.3f));
    g.fillPath (hexagon (led));

    g.setFont (font (14.0f, true));
    g.setColour (on ? Colours::text : Colours::textDim);
    g.drawText (getButtonText(), r.withTrimmedLeft (r.getHeight() * 0.5f + 9.0f).withTrimmedRight (7.0f),
                juce::Justification::centred, false);
}

//==============================================================================
SegmentedChoice::SegmentedChoice (juce::RangedAudioParameter& param, juce::StringArray l)
    : SegmentedChoice (std::move (l))
{
    attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v) { setSelectedIndex (juce::roundToInt (v)); }, nullptr);
    attachment->sendInitialUpdate();
}

SegmentedChoice::SegmentedChoice (juce::StringArray l) : labels (std::move (l))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedChoice::setSelectedIndex (int index)
{
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

int SegmentedChoice::indexAt (juce::Point<float> p) const
{
    if (labels.isEmpty()) return -1;
    const float w = (float) getWidth() / (float) labels.size();
    return juce::jlimit (0, labels.size() - 1, (int) (p.x / w));
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const int index = indexAt (e.position);
    if (attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) index);
    else
        setSelectedIndex (index);
    if (onSelect != nullptr)
        onSelect (index);
}

void SegmentedChoice::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.position);
    if (h != hovered) { hovered = h; repaint(); }
}

void SegmentedChoice::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto outline = hexCapsule (r);
    g.setColour (Colours::inset);
    g.fillPath (outline);
    g.setColour (Colours::panelBorder.brighter (0.2f));
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    const float w = r.getWidth() / (float) labels.size();
    for (int i = 0; i < labels.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.5f);

        if (i == selected)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (hexCapsule (r.reduced (2.5f)));
            g.setColour (Colours::accent.withAlpha (0.3f));
            g.fillRect (seg.expanded (1.0f));
            g.setGradientFill (honeyGradient (seg));
            g.fillRect (seg);
        }
        else if (i == hovered)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (hexCapsule (r.reduced (2.5f)));
            g.setColour (Colours::accent.withAlpha (0.08f));
            g.fillRect (seg);
        }

        if (i > 0 && i != selected && i - 1 != selected)
        {
            g.setColour (Colours::panelBorder);
            g.drawVerticalLine ((int) seg.getX() - 2, seg.getY() + 5.0f, seg.getBottom() - 5.0f);
        }

        g.setFont (font (juce::jmin (16.0f, r.getHeight() * 0.6f), true));
        g.setColour (i == selected ? juce::Colour (0xff1a0d02) : (i == hovered ? Colours::text : Colours::textDim));
        g.drawText (labels[i], seg, juce::Justification::centred, false);
    }
}

//==============================================================================
Footswitch::Footswitch (juce::RangedAudioParameter& param, const juce::String& c, juce::Colour led,
                        bool ledShowsInverse, std::function<bool()> isMomentary)
    : caption (c), ledColour (led), inverse (ledShowsInverse), momentary (std::move (isMomentary)),
      attachment (param, [this] (float v) { value = v >= 0.5f; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Footswitch::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    if (momentary && momentary())
    {
        holding = true;
        attachment.beginGesture();
        attachment.setValueAsPartOfGesture (1.0f);
    }
    else
    {
        attachment.setValueAsCompleteGesture (value ? 0.0f : 1.0f);
    }
    repaint();
}

void Footswitch::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
    if (holding)
    {
        holding = false;
        attachment.setValueAsPartOfGesture (0.0f);
        attachment.endGesture();
    }
    repaint();
}

void Footswitch::setLitExternally (bool shouldBeLit)
{
    if (shouldBeLit != externallyLit)
    {
        externallyLit = shouldBeLit;
        repaint();
    }
}

void Footswitch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool lit = (inverse ? ! value : value) || externallyLit;

    // Hex LED jewel
    const auto led = juce::Rectangle<float> (13.0f, 12.0f).withCentre ({ r.getCentreX(), r.getY() + 8.0f });
    r.removeFromTop (18.0f);
    if (lit)
    {
        juce::ColourGradient glow (ledColour.withAlpha (0.7f), led.getCentre(),
                                   ledColour.withAlpha (0.0f), led.getCentre().translated (18.0f, 0.0f), true);
        g.setGradientFill (glow);
        g.fillEllipse (led.expanded (13.0f));
    }
    g.setColour (lit ? ledColour : ledColour.withAlpha (0.16f));
    g.fillPath (hexagon (led, true));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (hexagon (led, true), juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.6f : 0.1f));
    g.fillEllipse (led.reduced (4.0f).translated (-1.0f, -1.5f));

    // Caption in the metal font
    auto text = r.removeFromBottom (20.0f);
    g.setFont (displayFont (19.0f));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawText (caption, text.translated (1.0f, 1.5f), juce::Justification::centred, false);
    if (lit)
        g.setGradientFill (honeyGradient (text.withSizeKeepingCentre (text.getWidth(), 16.0f)));
    else
        g.setColour (Colours::textDim);
    g.drawText (caption, text, juce::Justification::centred, false);

    // Stomp: hex nut housing with a worn metal button
    const float size = juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
    auto outer = r.withSizeKeepingCentre (size, size);
    const auto c = outer.getCentre();

    const auto nut = hexagon (outer, true);
    juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 12, { 0, 5 }).drawForPath (g, nut);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a4a3a), c.x, outer.getY(),
                                             juce::Colour (0xff120d0a), c.x, outer.getBottom(), false));
    g.fillPath (nut);
    g.setColour (lit ? ledColour.withAlpha (0.6f) : Colours::panelBorder.brighter (0.4f));
    g.strokePath (nut, juce::PathStrokeType (1.2f));

    auto inner = outer.reduced (size * 0.2f).translated (0.0f, pressed ? 1.5f : 0.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8cdb8), inner.getX(), inner.getY(),
                                             juce::Colour (0xff4a4036), inner.getRight(), inner.getBottom(), false));
    g.fillEllipse (inner);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (inner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawEllipse (inner.reduced (size * 0.06f), 0.8f);
    drawGrime (g, inner, 3.0f);
}

//==============================================================================
MiniSwitch::MiniSwitch (const juce::String& caption) : juce::ToggleButton (caption)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MiniSwitch::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();

    auto text = r.removeFromBottom (14.0f);
    g.setFont (font (11.5f, true));
    g.setColour (on ? Colours::accentBright : Colours::textDim);
    g.drawText (getButtonText(), text, juce::Justification::centred, false);

    // Hex nut + slot
    const auto slot = r.withSizeKeepingCentre (14.0f, juce::jmin (30.0f, r.getHeight() - 2.0f));
    const auto c = slot.getCentre();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff55585f), c.x, slot.getY(),
                                             juce::Colour (0xff1c1d21), c.x, slot.getBottom(), false));
    g.fillRoundedRectangle (slot, 7.0f);
    g.setColour (isMouseOver ? Colours::textFaint : juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (slot, 7.0f, 1.0f);

    // Lever: up = on
    const float leverY = on ? slot.getY() + 7.0f : slot.getBottom() - 7.0f;
    const auto knob = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ c.x, leverY });
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawLine (c.x, c.y, c.x, leverY, 3.0f);
    g.setGradientFill (juce::ColourGradient (on ? Colours::accentBright : juce::Colour (0xffd8dadf), knob.getX(), knob.getY(),
                                             on ? Colours::accentDeep : juce::Colour (0xff6a6d74), knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);
}

//==============================================================================
LevelMeter::LevelMeter (const juce::String& c) : caption (c) {}

void LevelMeter::update (float left, float right)
{
    const float in[2] = { left, right };
    for (size_t i = 0; i < 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (in[i], -60.0f);
        const float norm = juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f);
        level[i] = norm > level[i] ? norm : juce::jmax (norm, level[i] - 0.025f);

        if (level[i] >= hold[i]) { hold[i] = level[i]; holdTicks[i] = 45; }
        else if (--holdTicks[i] <= 0) hold[i] = juce::jmax (level[i], hold[i] - 0.01f);
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setFont (displayFont (17.0f));
    g.setColour (Colours::textDim);
    g.drawText (caption, r.removeFromLeft (32.0f), juce::Justification::centredLeft, false);

    const float barH = juce::jmin (6.0f, (r.getHeight() - 4.0f) * 0.5f);
    const float zeroDb = juce::jmap (0.0f, -60.0f, 6.0f, 0.0f, 1.0f);

    if (zoneMax > zoneMin)
    {
        const float x0 = r.getX() + r.getWidth() * juce::jmap (zoneMin, -60.0f, 6.0f, 0.0f, 1.0f);
        const float x1 = r.getX() + r.getWidth() * juce::jmap (zoneMax, -60.0f, 6.0f, 0.0f, 1.0f);
        g.setColour (Colours::accentBright.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (x0, r.getCentreY() - barH - 4.0f, x1 - x0, 2.0f * barH + 8.0f));
    }

    // Segmented honey bars
    const float segW = 4.0f, gap = 1.5f;
    const int segs = (int) (r.getWidth() / (segW + gap));
    for (size_t i = 0; i < 2; ++i)
    {
        const float y = r.getCentreY() - barH - 1.0f + (float) i * (barH + 2.0f);
        for (int s = 0; s < segs; ++s)
        {
            const float prop = (float) s / (float) segs;
            const auto seg = juce::Rectangle<float> (r.getX() + (float) s * (segW + gap), y, segW, barH);
            const bool litSeg = prop < level[i];
            juce::Colour col = prop < 0.7f ? Colours::meterLow.interpolatedWith (Colours::meterMid, prop / 0.7f)
                                           : (prop < zeroDb ? Colours::meterMid : Colours::meterHigh);
            g.setColour (litSeg ? col : Colours::inset.brighter (0.15f));
            g.fillRect (seg);
        }

        if (hold[i] > 0.01f)
        {
            const float hx = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, hold[i]);
            g.setColour (hold[i] >= zeroDb ? Colours::meterHigh : Colours::text.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (2.0f, barH).withCentre ({ hx - 1.0f, y + barH * 0.5f }));
        }
    }

    g.setColour (Colours::textFaint);
    const float zx = r.getX() + r.getWidth() * zeroDb;
    g.drawVerticalLine ((int) zx, r.getCentreY() - barH - 3.0f, r.getCentreY() + barH + 3.0f);
}

//==============================================================================
PitchScope::PitchScope() : history (180, 0.0f) {}

void PitchScope::push (float semitones, bool isActive)
{
    if (! primed)
    {
        std::fill (history.begin(), history.end(), semitones);
        primed = true;
    }
    history[(size_t) writeIndex] = semitones;
    writeIndex = (writeIndex + 1) % (int) history.size();
    active = isActive;
    current = semitones;
    repaint();
}

void PitchScope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto frame = chamfered (r, 6.0f);
    g.setColour (Colours::inset);
    g.fillPath (frame);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (frame);
        drawHoneycomb (g, r, 10.0f, Colours::accent.withAlpha (0.05f), 0.8f);
    }
    g.setColour (Colours::panelBorder.brighter (0.2f));
    g.strokePath (frame, juce::PathStrokeType (1.0f));

    auto plot = r.reduced (8.0f, 8.0f).withTrimmedRight (44.0f);
    const float range = 26.0f;
    auto yFor = [&] (float st) { return juce::jmap (juce::jlimit (-range, range, st), -range, range, plot.getBottom(), plot.getY()); };

    // Grid: octaves
    g.setFont (font (12.0f, true));
    for (int st : { -24, -12, 0, 12, 24 })
    {
        const float y = yFor ((float) st);
        g.setColour (st == 0 ? Colours::textFaint.withAlpha (0.6f) : Colours::panelBorder);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (Colours::textFaint);
        g.drawText ((st > 0 ? "+" : "") + juce::String (st), juce::Rectangle<float> (plot.getRight() + 4.0f, y - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }

    // Trace
    juce::Path trace;
    const int n = (int) history.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = history[(size_t) ((writeIndex + i) % n)];
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
        if (i == 0) trace.startNewSubPath (x, yFor (v));
        else        trace.lineTo (x, yFor (v));
    }

    const auto col = active ? Colours::accent : Colours::textFaint;
    g.setColour (col.withAlpha (0.22f));
    g.strokePath (trace, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (active)
        g.setGradientFill (honeyGradient (plot));
    else
        g.setColour (col);
    g.strokePath (trace, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Current value
    const float y = yFor (current);
    g.setColour (col);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ plot.getRight(), y }));

    g.setFont (font (13.0f, true));
    g.setColour (active ? Colours::accentBright : Colours::textFaint);
    g.drawText ((current > 0.05f ? "+" : "") + juce::String (current, 1) + " st",
                r.reduced (8.0f, 4.0f).removeFromTop (16.0f), juce::Justification::topLeft, false);
}

//==============================================================================
PresetBar::PresetBar (PresetManager& pm) : presets (pm)
{
    addAndMakeVisible (bankTabs);
    bankTabs.setTooltip ("FACTORY = built-in presets, USER = your saved presets");
    bankTabs.onSelect = [this] (int index)
    {
        setBank (index == 1);
        showMenuForBank();
    };

    for (auto* b : { &prevButton, &nextButton, &nameButton, &saveButton, &menuButton })
    {
        addAndMakeVisible (*b);
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    prevButton.setTooltip ("Previous preset in this bank");
    nextButton.setTooltip ("Next preset in this bank");
    nameButton.setTooltip ("Browse presets");
    saveButton.setTooltip ("Save preset (factory presets are saved as a new user preset)");
    menuButton.setTooltip ("Preset actions");

    prevButton.onClick = [this] { presets.loadPreviousPreset (userBank); refresh(); };
    nextButton.onClick = [this] { presets.loadNextPreset (userBank); refresh(); };
    nameButton.onClick = [this] { showMenuForBank(); };
    menuButton.onClick = [this] { showActionsMenu(); };
    saveButton.onClick = [this]
    {
        const auto name = presets.getCurrentPresetName();
        if (presets.isUserPreset (name))
        {
            presets.saveUserPreset (name);
            refresh();
        }
        else
        {
            saveAs();
        }
    };

    refresh();
}

void PresetBar::setBank (bool user)
{
    userBank = user;
    bankTabs.setSelectedIndex (user ? 1 : 0);
}

void PresetBar::refresh()
{
    const auto name = presets.getCurrentPresetName();
    const bool dirty = presets.isDirty();
    if (name != shownName || dirty != shownDirty)
    {
        // The bank follows a newly loaded preset (browsing, saving, host state restore).
        if (name != shownName)
            setBank (presets.isUserPreset (name));

        shownName = name;
        shownDirty = dirty;
        nameButton.setButtonText (dirty ? name + " *" : name);
        const auto description = presets.getPresetDescription (name);
        nameButton.setTooltip (description.isNotEmpty() ? description : juce::String ("Browse presets"));
    }
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int h = r.getHeight();
    bankTabs.setBounds (r.removeFromLeft (130).reduced (0, 2));
    r.removeFromLeft (8);
    prevButton.setBounds (r.removeFromLeft (h));
    r.removeFromLeft (4);
    menuButton.setBounds (r.removeFromRight (h + 6));
    r.removeFromRight (4);
    saveButton.setBounds (r.removeFromRight (64));
    r.removeFromRight (4);
    nextButton.setBounds (r.removeFromRight (h));
    r.removeFromRight (4);
    nameButton.setBounds (r);
}

void PresetBar::paint (juce::Graphics&) {}

void PresetBar::showMenuForBank()
{
    juce::PopupMenu menu;
    const auto current = presets.getCurrentPresetName();
    auto load = [this] (const juce::String& n) { return [this, n] { presets.loadPreset (n); refresh(); }; };

    if (! userBank)
    {
        // One submenu per category keeps the factory list short; the current category is ticked.
        for (const auto& category : presets.getFactoryCategories())
        {
            juce::PopupMenu sub;
            const auto names = presets.getFactoryPresetNames (category);
            for (const auto& n : names)
                sub.addItem (n, true, n == current, load (n));
            menu.addSubMenu (category, sub, true, nullptr, names.contains (current));
        }
    }
    else
    {
        const auto user = presets.getUserPresetNames();
        if (user.isEmpty())
            menu.addItem ("No user presets yet", false, false, nullptr);
        for (const auto& n : user)
            menu.addItem (n, true, n == current, load (n));

        menu.addSeparator();
        menu.addItem ("Save Current Sound As...", [this] { saveAs(); });
        menu.addItem ("Import Preset...", [this] { importPreset(); });
        menu.addItem ("Open Presets Folder", [] { PresetManager::getPresetsDirectory().startAsProcess(); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton)
                                                  .withMinimumWidth (nameButton.getWidth()));
}

void PresetBar::showActionsMenu()
{
    const auto current = presets.getCurrentPresetName();
    juce::PopupMenu menu;
    menu.addItem ("Save As...", [this] { saveAs(); });
    menu.addItem ("Delete", presets.isUserPreset (current), false, [this] { confirmDelete(); });
    menu.addSeparator();
    menu.addItem ("Import Preset...", [this] { importPreset(); });
    menu.addItem ("Export Preset...", [this] { exportPreset(); });
    menu.addItem ("Open Presets Folder", [] { PresetManager::getPresetsDirectory().startAsProcess(); });
    menu.addSeparator();
    menu.addItem ("Reset to Init", [this] { presets.loadPreset ("Init"); refresh(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void PresetBar::saveAs()
{
    auto* window = new juce::AlertWindow ("Save Preset", "Enter a name for the preset:", juce::MessageBoxIconType::NoIcon, this);
    auto initial = presets.getCurrentPresetName();
    if (presets.isFactoryPreset (initial))
        initial = initial == "Init" ? juce::String ("My Preset") : initial + " (edit)";

    window->addTextEditor ("name", initial);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
    {
        if (result == 1 && safe != nullptr)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (name.isNotEmpty())
            {
                if (safe->presets.isFactoryPreset (name))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset",
                                                            "\"" + name + "\" is a factory preset name. Please choose another name.");
                else
                    safe->presets.saveUserPreset (name);
                safe->refresh();
            }
        }
    }), true);
}

void PresetBar::confirmDelete()
{
    const auto name = presets.getCurrentPresetName();
    if (! presets.isUserPreset (name))
        return;

    juce::Component::SafePointer<PresetBar> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete Preset",
                                        "Delete \"" + name + "\"? This cannot be undone.", "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe, name] (int result)
                                        {
                                            if (result == 1 && safe != nullptr)
                                            {
                                                safe->presets.deleteUserPreset (name);
                                                safe->refresh();
                                            }
                                        }));
}

void PresetBar::importPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Import Swarmness preset", PresetManager::getPresetsDirectory(),
                                                   "*" + PresetManager::extension);
    juce::Component::SafePointer<PresetBar> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              if (! safe->presets.importPreset (fc.getResult()))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                                          "This file is not a valid Swarmness preset.");
                              safe->refresh();
                          });
}

void PresetBar::exportPreset()
{
    const auto file = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                          .getChildFile (juce::File::createLegalFileName (presets.getCurrentPresetName()) + PresetManager::extension);
    chooser = std::make_unique<juce::FileChooser> ("Export Swarmness preset", file, "*" + PresetManager::extension);
    juce::Component::SafePointer<PresetBar> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              safe->presets.exportPreset (fc.getResult().withFileExtension (PresetManager::extension));
                          });
}

//==============================================================================
InfoOverlay::InfoOverlay()
{
    setInterceptsMouseClicks (true, false);
}

void InfoOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.82f));

    auto r = getLocalBounds().toFloat().reduced (60.0f, 40.0f);
    Theme::drawPanel (g, r, 12.0f);
    r.reduce (28.0f, 20.0f);

    g.setFont (displayFont (32.0f));
    const auto titleArea = r.removeFromTop (34.0f);
    g.setGradientFill (honeyGradient (titleArea));
    g.drawText ("Swarmness  " + juce::String (JucePlugin_VersionString), titleArea, juce::Justification::centredLeft, false);
    r.removeFromTop (6.0f);

    struct Item { const char* title; const char* body; };
    static const Item items[] =
    {
        { "STING",    "Hold +1 OCT / +2 OCT (or latch them) for a violent octave. RISE = glide in, FALL = glide back on release. ANGER = detuned dissonance, "
                      "FRENZY = random pitch jumps, BUZZ = all-pass feedback + ring-mod-like AM, MIX = dry / octave blend. DIVE = shift down." },
        { "RAW",      "STING and HIVE: lo-fi pedal character on top of the in-tune shifter - cheap converters and a slow pitch warble "
                      "(deeper at low TRACKING in HIVE). Off = clean. HIVE runs in parallel: it harmonises the note you play, not the octave." },
        { "HIVE",     "Harmony voices: PITCH (-12..+12 st, SNAP = semitones), DRONE = main voice, QUEEN = its octave, TONE. TRACKING low = lag "
                      "and tone clusters. TRAILS = repeats climbing / falling by PITCH, TIME = their spacing (SYNC = tempo). MIX 100% = HIVE only." },
        { "VENOM",    "The VENOM footswitch pushes the TRAILS into self-oscillation - even with HIVE or the plug-in off. "
                      "LINK mini switches next to the octaves make VENOM engage +1 / +2 OCT too." },
        { "SWARM",    "Stereo chorus with bucket-brigade colour. DEEP = 8 voices with feedback. MIX 50% = dry and chorus both full, 100% = vibrato." },
        { "SMOKE",    "Jumbo fuzz. VOICE: DOWN doom / MID / UP scream. SCOOP = mid cut, GLARE = gated octave-up, GATE = starved sputter, "
                      "BLEND = clean under the fuzz. POST = after the pitch effects (off = before)." },
        { "WINGS",    "Rhythmic gate: HARD = stutter, off = tremolo. SYNC locks to the host tempo (DIV)." },
        { "SWITCHES", "MOMENTARY = active while held, LATCH = click on / off. Footswitches work even while bypassed. MIDI-learn them in your DAW." },
        { "LEVELS",   "INPUT sets how hard the effects are hit (aim for the green zone of the IN meter); it is compensated at the output. "
                      "VOLUME = output level." },
        { "PRESETS",  "FACTORY / USER tabs pick the bank that the list and the < > arrows browse. SAVE stores your sound in USER "
                      "(an edited factory preset becomes a new user preset). Hover the name for the preset's description." },
    };

    for (const auto& item : items)
    {
        auto row = r.removeFromTop (juce::jmin (50.0f, r.getHeight()));
        g.setFont (displayFont (19.0f));
        g.setColour (Colours::accentBright);
        g.drawText (item.title, row.removeFromLeft (120.0f), juce::Justification::topLeft, false);
        g.setFont (font (16.0f));
        g.setColour (Colours::text);
        g.drawFittedText (item.body, row.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }

    g.setFont (font (14.0f));
    g.setColour (Colours::textDim);
    g.drawText ("Click anywhere to close", getLocalBounds().toFloat().reduced (60.0f, 48.0f), juce::Justification::bottomRight, false);
}
