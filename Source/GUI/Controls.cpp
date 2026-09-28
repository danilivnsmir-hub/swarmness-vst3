#include "Controls.h"
#include "../Parameters.h"
#include <limits>

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
    MidiLearnable::tag (*this, id);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= getHeight() - 20 && isEnabled() && ! e.mods.isPopupMenu())
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

    juce::Component::SafePointer<Knob> safe (this);
    auto finish = [safe] (bool apply)
    {
        if (safe == nullptr || safe->valueEditor == nullptr) return;
        if (apply)
        {
            const auto entered = safe->valueEditor->getText().trim();
            if (entered.isNotEmpty())
                safe->slider.setValue (safe->slider.snapValue (safe->slider.getValueFromText (entered), juce::Slider::notDragging),
                                       juce::sendNotificationSync);
        }
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeValueEditor(); });
    };
    ed->onReturnKey = [finish] { finish (true); };
    ed->onEscapeKey = [finish] { finish (false); };
    ed->onFocusLost = [finish] { finish (true); };
}

void Knob::closeValueEditor()
{
    valueEditor.reset();
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
    MidiLearnable::tag (*this, id);
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
    g.setFont (font (compact ? 13.0f : 14.5f, true));
    g.setColour (Colours::textDim);
    g.drawText (caption, r.removeFromTop (18.0f), juce::Justification::centred, false);

    auto value = slider.getTextFromValue (slider.getValue());
    if (compact)
        value = value.upToFirstOccurrenceOf (" ", false, false);
    g.setFont (font (compact ? 13.0f : 15.0f, true));
    g.setColour (Colours::accentBright);
    g.drawText (value, r.removeFromBottom (18.0f), juce::Justification::centred, false);
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
    MidiLearnable::tag (*this, param.paramID);
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
    if (! isEnabled() || e.mods.isPopupMenu()) return;
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
    MidiLearnable::tag (*this, param.paramID);
}

void Footswitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;   // MIDI menu (the editor)
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

void Footswitch::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    pressed = false;
    if (holding)
    {
        holding = false;
        attachment.setValueAsPartOfGesture (0.0f);
        attachment.endGesture();
    }
    repaint();
}

void Footswitch::setLearning (bool isLearning)
{
    if (isLearning != learning || isLearning)
    {
        learning = isLearning;
        repaint();
    }
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

    // Stomp: charred hex-nut housing with a dark iron button, like the knobs
    const float size = juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
    auto outer = r.withSizeKeepingCentre (size, size);
    const auto c = outer.getCentre();

    const auto nut = hexagon (outer, true);
    juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 14, { 0, 6 }).drawForPath (g, nut);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2c20), c.x, outer.getY(),
                                             juce::Colour (0xff0b0806), c.x, outer.getBottom(), false));
    g.fillPath (nut);
    g.setColour (lit ? ledColour.withAlpha (0.75f) : Colours::panelBorder.brighter (0.25f));
    g.strokePath (nut, juce::PathStrokeType (lit ? 1.6f : 1.2f));

    auto inner = outer.reduced (size * 0.19f).translated (0.0f, pressed ? 1.5f : 0.0f);
    if (lit)
    {
        g.setColour (ledColour.withAlpha (0.28f));
        g.fillEllipse (inner.expanded (4.0f));
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff54442f), inner.getX(), inner.getY(),
                                             juce::Colour (0xff120d09), inner.getRight(), inner.getBottom(), false));
    g.fillEllipse (inner);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (inner, 1.2f);

    // machined rings + engraved hex, warm edge light
    for (float k : { 0.14f, 0.28f })
    {
        g.setColour (Colours::accentBright.withAlpha (0.07f));
        g.drawEllipse (inner.reduced (size * k * 0.5f), 0.8f);
    }
    const auto engraved = hexagon (inner.reduced (inner.getWidth() * 0.3f), true);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (engraved, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour ((lit ? ledColour : Colours::accent).withAlpha (lit ? 0.7f : 0.25f));
    g.strokePath (engraved, juce::PathStrokeType (1.1f));
    g.setGradientFill (juce::ColourGradient (Colours::accentBright.withAlpha (lit ? 0.45f : 0.2f), inner.getX(), inner.getY(),
                                             juce::Colours::transparentBlack, inner.getCentreX(), inner.getCentreY(), false));
    g.drawEllipse (inner.reduced (1.0f), 1.2f);
    drawGrime (g, inner, 3.0f);

    if (learning)
    {
        const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.012f);
        g.setColour (Colours::accentBright.withAlpha (0.35f + 0.55f * pulse));
        g.strokePath (hexagon (outer.expanded (4.0f), true), juce::PathStrokeType (2.5f));
        g.setFont (font (12.0f, true));
        g.drawText ("MIDI?", outer.withHeight (14.0f).translated (0.0f, -2.0f), juce::Justification::centred, false);
    }
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
PitchScope::PitchScope() : history (180, 0.0f), stackHistory (180, std::numeric_limits<float>::quiet_NaN()) {}

void PitchScope::push (float semitones, bool isActive, bool stackOn, float stackSemitones)
{
    if (! primed)
    {
        std::fill (history.begin(), history.end(), semitones);
        primed = true;
    }
    history[(size_t) writeIndex] = semitones;
    stackHistory[(size_t) writeIndex] = stackOn ? stackSemitones : std::numeric_limits<float>::quiet_NaN();
    writeIndex = (writeIndex + 1) % (int) history.size();
    active = isActive;
    current = semitones;
    stackNow = stackOn;
    currentStack = stackSemitones;
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

    // STACK voice: a second trace while it sounds
    {
        juce::Path stack;
        bool drawing = false;
        for (int i = 0; i < n; ++i)
        {
            const float v = stackHistory[(size_t) ((writeIndex + i) % n)];
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
            if (std::isnan (v)) { drawing = false; continue; }
            if (! drawing) { stack.startNewSubPath (x, yFor (v)); drawing = true; }
            else           stack.lineTo (x, yFor (v));
        }
        if (! stack.isEmpty())
        {
            g.setColour (Colours::venom.withAlpha (0.25f));
            g.strokePath (stack, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Colours::venom);
            g.strokePath (stack, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (stackNow)
        {
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ plot.getRight(), yFor (currentStack) }));
            g.setFont (font (13.0f, true));
            g.drawText ("+ " + juce::String (currentStack > 0.05f ? "+" : "") + juce::String (currentStack, 1) + " st",
                        r.reduced (8.0f, 4.0f).removeFromTop (16.0f).withTrimmedLeft (70.0f), juce::Justification::topLeft, false);
        }
    }

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
        { "SHIFT",    "Pitch shifter: on = SHIFT A all the time; the footswitches engage it while held (any interval, -24..+24 st). RISE / FALL glide, MIX = replace or double, "
                      "STACK = A + B together play both intervals. ANGER = a sour second voice, FRENZY = random jumps, BUZZ = phaser / ring-mod grind." },
        { "HIVE",     "Harmonies of whatever reaches it (after SHIFT: of the shifted note). DRONE at PITCH and QUEEN (its octave), TRACKING tight..laggy. "
                      "VENOM switches it on while held." },
        { "TRAILS",   "Repeats of the DRONE, shaped by STEPS like a pattern tremolo: each bar = one repeat (LEVEL, 0 = silent) and its MOVE "
                      "(= hold, up / down by PITCH, ? random, < backwards). GATE chops every step (0 = full repeats). DRY = repeats of your note (a delay). FILL = ready-made patterns. TIME / SYNC = step length. "
                      "The VENOM footswitch = self-oscillation; LINK drags SHIFT A / B in." },
        { "MANGLE",   "HIVE's MANGLE is one knob: sour detuned voices first, then random pitch jumps, then buzz / AM on top. "
                      "RAW = cheap-pedal-DSP character, DETUNE = width, MIX = dry vs voices (100% = voices only)." },
        { "SWARM",    "Stereo chorus with bucket-brigade colour (DEEP = 8 voices with feedback). WINGS = rhythmic gate: HARD = stutter, off = tremolo, SYNC = host tempo." },
        { "SMOKE",    "Jumbo fuzz. VOICE: DOWN doom / MID / UP scream. SCOOP = mid cut, GLARE = gated octave-up, GATE = starved sputter, SAG = breathing "
                      "(the pick sags, the note blooms), CLEAN = clean signal under the fuzz." },
        { "WASP",     "Tight overdrive in front of the AMP (asymmetric hard clipping): DRIVE = tight boost .. square overdrive, ATTACK = how tight the low end is "
                      "before the clipping, BRIGHT = voicing, VOLUME (5 = about unity), GATE = noise gate keyed from the guitar." },
        { "AMP",      "Three amps: CLEAN = CHROME (crystal clean), CRUNCH = BRIT (barking crunch), LEAD = STEEL (tight high gain). "
                      "NAM = a Neural Amp Modeler capture with its own INPUT / EQ / OUTPUT knobs (all at 5 = the capture as it is) - LOAD .NAM or drop one, find thousands on TONE3000. GATE = noise gate keyed from the guitar (on the amp's input and output)." },
        { "CAB",      "Speaker cabinet: four modelled cabinets (MIC = cap..edge, DISTANCE = grille..room) or your own IRs in two slots A / B "
                      "(LOAD IR, TONE3000 or drop a WAV on a slot; A / B MIX blends them, time-aligned; INV B flips B's phase)." },
        { "CHAIN",    "The strip under the header is the signal chain. Drag a block to reorder it (fuzz before or after the pitch, reverb into the fuzz...), "
                      "click it to open its page, click its LED to switch it on / off, right-click for MIDI learn. Drag it UP / DOWN for parallel paths A / B (an empty path = dry), "
                      "the knob at the merge balances A and B. Order and paths are saved with presets." },
        { "EQ",       "COMB = 10-band graphic EQ (+-12 dB) with LEVEL. CARVE = parametric: 24 dB/oct LOW / HIGH CUT, shelves and 3 bells - drag the nodes, "
                      "wheel = Q, double-click = reset; the output spectrum runs behind the curve." },
        { "CRYPT",    "Reverb: ROOM / PLATE / HALL / ABYSS or your own IR (LOAD IR or drop a file). DUCK dips the tail while you play, "
                      "LOW CUT keeps it out of the low end. Switching it off lets the tail ring out." },
        { "MIDI",     "Right-click ANY control for MIDI learn: switches toggle on each press, selectors step, knobs follow the CC. "
                      "One pedal can drive several controls (e.g. ON and WINGS)." },
        { "LEVELS",   "INPUT = input gain: how hard the effects and amps are hit (aim for the green zone of the IN meter). "
                      "VOLUME = output level. Footswitches: MOMENTARY = while held, LATCH = click on / off (they work even while bypassed)." },
        { "PRESETS",  "FACTORY / USER tabs pick the bank that the list and the < > arrows browse. SAVE stores your sound in USER "
                      "(an edited factory preset becomes a new user preset). Hover the name for the preset's description." },
    };

    for (const auto& item : items)
    {
        auto row = r.removeFromTop (juce::jmin (42.0f, r.getHeight()));
        g.setFont (displayFont (19.0f));
        g.setColour (Colours::accentBright);
        g.drawText (item.title, row.removeFromLeft (120.0f), juce::Justification::topLeft, false);
        g.setFont (font (15.0f));
        g.setColour (Colours::text);
        g.drawFittedText (item.body, row.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }

    g.setFont (font (14.0f));
    g.setColour (Colours::textDim);
    g.drawText ("Click anywhere to close", getLocalBounds().toFloat().reduced (60.0f, 48.0f), juce::Justification::bottomRight, false);
}

//==============================================================================
StepGrid::StepGrid (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setTooltip ("STEPS - a pattern for the repeats (like a pattern tremolo): every bar is one repeat after the note "
                "(the pattern restarts on each picked note, or follows the song with SYNC). "
                "Drag the bars = LEVEL of each repeat (0 = silent, the tail keeps running), double-click = on / off. "
                "Click the symbol below = MOVE: = hold, up / down by PITCH, ? random chord tone, < backwards. "
                "STEPS -/+ = pattern length, FILL = ready-made patterns.");
    setRepaintsOnMouseActivity (false);
}

float StepGrid::level (int step) const { return state.getRawParameterValue (ParamIDs::trLevels[step])->load() * 0.01f; }
int StepGrid::move (int step) const    { return juce::roundToInt (state.getRawParameterValue (ParamIDs::trMoves[step])->load()); }
int StepGrid::numSteps() const         { return juce::jlimit (1, kSteps, juce::roundToInt (state.getRawParameterValue (ParamIDs::trSteps)->load())); }

void StepGrid::refresh (int playingStep)
{
    bool changed = numSteps() != shownSteps || playingStep != shownPlaying;
    for (int k = 0; k < kSteps && ! changed; ++k)
        changed = std::abs (level (k) - shownLevel[(size_t) k]) > 1.0e-4f || move (k) != shownMove[(size_t) k];
    if (! changed)
        return;
    shownSteps = numSteps();
    shownPlaying = playingStep;
    for (int k = 0; k < kSteps; ++k)
    {
        shownLevel[(size_t) k] = level (k);
        shownMove[(size_t) k] = move (k);
    }
    repaint();
}

juce::Rectangle<float> StepGrid::headerArea() const { return getLocalBounds().toFloat().withHeight (20.0f); }
juce::Rectangle<float> StepGrid::movesArea() const  { return getLocalBounds().toFloat().removeFromBottom (18.0f); }
juce::Rectangle<float> StepGrid::barsArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (23.0f);
    r.removeFromBottom (19.0f);
    return r;
}
juce::Rectangle<float> StepGrid::minusArea() const { return { 50.0f, 1.0f, 18.0f, 18.0f }; }
juce::Rectangle<float> StepGrid::plusArea() const  { return { 92.0f, 1.0f, 18.0f, 18.0f }; }
juce::Rectangle<float> StepGrid::fillArea() const  { return headerArea().removeFromRight (74.0f).reduced (0.0f, 1.0f); }

int StepGrid::stepAt (float x) const
{
    const auto b = barsArea();
    return juce::jlimit (0, kSteps - 1, (int) std::floor ((x - b.getX()) / b.getWidth() * kSteps));
}

float StepGrid::levelAt (float y) const
{
    const auto b = barsArea();
    const float v = juce::jlimit (0.0f, 1.0f, (b.getBottom() - y) / b.getHeight());
    return v < 0.04f ? 0.0f : (v > 0.96f ? 1.0f : v);
}

void StepGrid::setParam (const char* id, float value, bool gesture)
{
    if (auto* p = state.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        if (gesture) p->endChangeGesture();
    }
}

void StepGrid::paintLevel (int step, float value)
{
    auto* p = state.getParameter (ParamIDs::trLevels[step]);
    if (p == nullptr)
        return;
    if (std::find (painting.begin(), painting.end(), p) == painting.end())
    {
        p->beginChangeGesture();
        painting.push_back (p);
    }
    p->setValueNotifyingHost (p->convertTo0to1 (value * 100.0f));
}

void StepGrid::endPainting()
{
    for (auto* p : painting)
        p->endChangeGesture();
    painting.clear();
    lastPaintStep = -1;
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    if (minusArea().contains (pos) || plusArea().contains (pos))
    {
        setParam (ParamIDs::trSteps, (float) juce::jlimit (1, kSteps, numSteps() + (plusArea().contains (pos) ? 1 : -1)));
        return;
    }
    if (fillArea().contains (pos))
    {
        showFillMenu();
        return;
    }
    const int step = stepAt (pos.x);
    if (movesArea().contains (pos))
    {
        if (e.mods.isPopupMenu())
            showMoveMenu (step);
        else
            setParam (ParamIDs::trMoves[step], (float) ((move (step) + 1) % ParamChoices::stepMoves.size()));
        return;
    }
    if (barsArea().expanded (0.0f, 3.0f).contains (pos))
    {
        if (e.mods.isPopupMenu())
        {
            showMoveMenu (step);
            return;
        }
        lastPaintStep = step;
        lastPaintLevel = levelAt (pos.y);
        paintLevel (step, lastPaintLevel);
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (lastPaintStep < 0)
        return;
    // paint every step between the last one and here, so fast drags leave no gaps
    const int step = stepAt (e.position.x);
    const float value = levelAt (e.position.y);
    const int from = lastPaintStep, dir = step >= from ? 1 : -1;
    for (int k = from; ; k += dir)
    {
        const float t = step == from ? 1.0f : (float) (k - from) / (float) (step - from);
        paintLevel (k, lastPaintLevel + t * (value - lastPaintLevel));
        if (k == step) break;
    }
    lastPaintStep = step;
    lastPaintLevel = value;
}

void StepGrid::mouseUp (const juce::MouseEvent&) { endPainting(); }

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! barsArea().contains (e.position))
        return;
    const int step = stepAt (e.position.x);
    setParam (ParamIDs::trLevels[step], level (step) > 0.01f ? 0.0f : 100.0f);
}

void StepGrid::showMoveMenu (int step)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Step " + juce::String (step + 1));
    for (int i = 0; i < ParamChoices::stepMoves.size(); ++i)
        m.addItem (i + 1, ParamChoices::stepMoves[i], true, move (step) == i);
    m.addSeparator();
    m.addItem (100, "Set all steps to this move");
    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, step] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r == 100)
        {
            const int mv = safe->move (step);
            for (int k = 0; k < kSteps; ++k)
                safe->setParam (ParamIDs::trMoves[k], (float) mv);
            return;
        }
        safe->setParam (ParamIDs::trMoves[step], (float) (r - 1));
    });
}

void StepGrid::showFillMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Ready-made patterns");
    for (int i = 0; i < ParamChoices::trailFills.size(); ++i)
        m.addItem (i + 1, ParamChoices::trailFills[i]);
    m.addSeparator();
    m.addItem (200, "All steps on");
    m.addItem (201, "Every second step");
    m.addItem (202, "Random levels");
    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r <= ParamChoices::trailFills.size())
        {
            safe->applyFill (r - 1);
            return;
        }
        juce::Random rnd;
        for (int k = 0; k < kSteps; ++k)
        {
            float v = 100.0f;
            if (r == 201) v = k % 2 == 0 ? 100.0f : 0.0f;
            if (r == 202) v = rnd.nextFloat() < 0.25f ? 0.0f : 30.0f + 70.0f * rnd.nextFloat();
            safe->setParam (ParamIDs::trLevels[k], v);
        }
    });
}

void StepGrid::applyFill (int fill)
{
    PresetManager::ValueMap values;
    PresetManager::writeTrailFill (values, fill);
    for (const auto& [id, v] : values)
        setParam (id.toRawUTF8(), v);
}

void StepGrid::paint (juce::Graphics& g)
{
    using namespace Theme;
    const int n = numSteps();

    // header: STEPS - n +      FILL
    g.setFont (font (13.0f, true));
    g.setColour (Colours::textDim);
    g.drawText ("STEPS", juce::Rectangle<float> (0.0f, 0.0f, 48.0f, 20.0f), juce::Justification::centredLeft, false);
    for (auto r : { minusArea(), plusArea() })
    {
        g.setColour (Colours::inset);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::panelBorder);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    g.setColour (Colours::text);
    g.drawText ("-", minusArea(), juce::Justification::centred, false);
    g.drawText ("+", plusArea(), juce::Justification::centred, false);
    g.setColour (Colours::accentBright);
    g.drawText (juce::String (n), juce::Rectangle<float> (68.0f, 0.0f, 24.0f, 20.0f), juce::Justification::centred, false);
    const auto fr = fillArea();
    g.setColour (Colours::inset);
    g.fillRoundedRectangle (fr, 3.0f);
    g.setColour (Colours::panelBorder);
    g.drawRoundedRectangle (fr, 3.0f, 1.0f);
    g.setColour (Colours::text);
    g.drawText ("FILL  v", fr, juce::Justification::centred, false);

    // bars
    const auto bars = barsArea();
    const float w = bars.getWidth() / kSteps;
    g.setColour (Colours::inset);
    g.fillRoundedRectangle (bars.expanded (1.0f), 3.0f);
    for (int k = 0; k < kSteps; ++k)
    {
        const bool active = k < n;
        const auto cell = juce::Rectangle<float> (bars.getX() + k * w, bars.getY(), w, bars.getHeight()).reduced (1.5f, 0.0f);
        if (k % 4 == 0 && k > 0)
        {
            g.setColour (Colours::panelBorder.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float> (cell.getX() - 1.5f, bars.getY(), 1.0f, bars.getHeight()));
        }
        const float v = level (k);
        const auto bar = cell.withTop (cell.getBottom() - juce::jmax (2.0f, v * cell.getHeight()));
        const bool playing = k == shownPlaying;
        auto colour = active ? (playing ? Colours::accentBright : Colours::accent) : Colours::textFaint;
        if (v <= 0.0f) colour = colour.withAlpha (0.35f);
        g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
        g.fillRoundedRectangle (bar, 1.5f);
        if (playing)
        {
            g.setColour (Colours::accentBright.withAlpha (0.25f));
            g.fillRoundedRectangle (cell, 2.0f);
        }

        // MOVE symbol
        const auto sym = juce::Rectangle<float> (bars.getX() + k * w, movesArea().getY(), w, movesArea().getHeight()).reduced (3.0f, 4.0f);
        g.setColour (active ? Colours::text : Colours::textFaint);
        juce::Path p;
        const float cx = sym.getCentreX(), cy = sym.getCentreY(), r = juce::jmin (sym.getWidth(), sym.getHeight()) * 0.5f;
        switch (move (k))
        {
            case 0: g.fillRect (juce::Rectangle<float> (cx - r, cy - 1.0f, 2.0f * r, 2.0f)); break;                          // hold
            case 1: p.addTriangle (cx - r, cy + r * 0.8f, cx + r, cy + r * 0.8f, cx, cy - r); g.fillPath (p); break;           // up
            case 2: p.addTriangle (cx - r, cy - r * 0.8f, cx + r, cy - r * 0.8f, cx, cy + r); g.fillPath (p); break;           // down
            case 3: g.setFont (font (13.0f, true)); g.drawText ("?", sym.expanded (3.0f), juce::Justification::centred, false); break;
            default: p.addTriangle (cx + r * 0.8f, cy - r, cx + r * 0.8f, cy + r, cx - r, cy); g.fillPath (p); break;          // reverse
        }
    }
}

//==============================================================================
SceneBar::SceneBar()
{
    setTooltip ("SCENES: four versions of the current preset's sound. Click to switch (glides, no clicks); a new scene starts as a "
                "copy of the one you are in, and your edits stay in the scene. The chain order is shared. Saved with the preset. "
                "Right-click: MIDI learn a pedal for this scene, copy the current scene here");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SceneBar::refresh()
{
    const int current = getCurrent != nullptr ? getCurrent() : 0;
    std::array<bool, kScenes> used {};
    for (int k = 0; k < kScenes; ++k)
        used[(size_t) k] = isUsed != nullptr && isUsed (k);
    if (current != shownCurrent || used != shownUsed)
    {
        shownCurrent = current;
        shownUsed = used;
        repaint();
    }
}

juce::Rectangle<float> SceneBar::buttonArea (int scene) const
{
    const float w = 112.0f, gap = 12.0f;
    const float total = kScenes * w + (kScenes - 1) * gap;
    const float x0 = ((float) getWidth() - total) * 0.5f;
    return { x0 + (float) scene * (w + gap), 4.0f, w, (float) getHeight() - 8.0f };
}

void SceneBar::mouseDown (const juce::MouseEvent& e)
{
    for (int k = 0; k < kScenes; ++k)
        if (buttonArea (k).contains (e.position))
        {
            if (e.mods.isPopupMenu()) { if (onRightClick != nullptr) onRightClick (k); }
            else if (onSelect != nullptr) onSelect (k);
            return;
        }
}

void SceneBar::paint (juce::Graphics& g)
{
    using namespace Theme;
    g.setFont (displayFont (17.0f));
    g.setColour (Colours::textDim);
    g.drawText ("SCENES", getLocalBounds().toFloat().withTrimmedLeft (18.0f), juce::Justification::centredLeft, false);

    static const char* letters[] { "A", "B", "C", "D" };
    for (int k = 0; k < kScenes; ++k)
    {
        const auto r = buttonArea (k);
        const bool current = k == shownCurrent, used = shownUsed[(size_t) k];
        auto shape = chamfered (r, 7.0f);
        if (current)
        {
            g.setGradientFill (juce::ColourGradient (Colours::accentBright, r.getX(), r.getY(), Colours::accentDeep, r.getX(), r.getBottom(), false));
            g.fillPath (shape);
        }
        else
        {
            g.setColour (Colours::inset);
            g.fillPath (shape);
        }
        g.setColour (current ? Colours::accentBright : (used ? Colours::accent.withAlpha (0.8f) : Colours::panelBorder));
        g.strokePath (shape, juce::PathStrokeType (current ? 1.6f : 1.2f));

        g.setFont (displayFont (20.0f));
        g.setColour (current ? Colours::background : (used ? Colours::text : Colours::textFaint));
        g.drawText (letters[k], r.withTrimmedBottom (describeMidi != nullptr && describeMidi (k).isNotEmpty() ? 10.0f : 0.0f),
                    juce::Justification::centred, false);
        if (describeMidi != nullptr)
            if (const auto midi = describeMidi (k); midi.isNotEmpty())
            {
                g.setFont (font (11.0f, true));
                g.setColour (current ? Colours::background.withAlpha (0.8f) : Colours::textDim);
                g.drawText (midi, r.withTrimmedTop (r.getHeight() - 14.0f), juce::Justification::centred, false);
            }
    }
}
