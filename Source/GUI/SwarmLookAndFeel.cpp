#include "SwarmLookAndFeel.h"

using namespace Theme;

SwarmLookAndFeel::SwarmLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::accentBright);
    setColour (juce::TextButton::buttonColourId, Colours::panelHi);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::accentBright);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::panelBorder);
    setColour (juce::TextEditor::backgroundColourId, Colours::inset);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, Colours::panelBorder);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::CaretComponent::caretColourId, Colours::accentBright);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelHi);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::outlineColourId, Colours::panelBorder);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Colours::inset);
    setColour (juce::Slider::textBoxOutlineColourId, Colours::accent);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.35f));
}

juce::Typeface::Ptr SwarmLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    return f.isBold() ? typefaceBold() : typefaceMedium();
}

//==============================================================================
void SwarmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto  centre = bounds.getCentre();
    const float radius = size * 0.5f - 2.0f;
    const float arcRadius  = radius - 2.5f;
    const float arcThick   = juce::jmax (2.5f, size * 0.055f);
    const float bodyRadius = radius * 0.70f;

    const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
    const bool hover   = slider.isMouseOverOrDragging();
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);

    // --- Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::inset);
    g.strokePath (track, juce::PathStrokeType (arcThick + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.strokePath (track, juce::PathStrokeType (arcThick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // --- Value arc
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (from, angle), juce::jmax (from, angle), true);

        g.setColour (Colours::accent.withAlpha (hover ? 0.28f : 0.18f));
        g.strokePath (value, juce::PathStrokeType (arcThick + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setGradientFill (juce::ColourGradient (Colours::accentDeep, bounds.getBottomLeft(),
                                                 Colours::accentBright, bounds.getTopRight(), false));
        g.strokePath (value, juce::PathStrokeType (arcThick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- Body shadow
    {
        juce::Path body;
        body.addEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), (int) (size * 0.12f), { 0, (int) (size * 0.04f) })
            .drawForPath (g, body);
    }

    // --- Body: brushed metal radial gradient
    const auto bodyRect = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    {
        juce::ColourGradient grad (juce::Colour (0xff3d4048), centre.x - bodyRadius * 0.4f, centre.y - bodyRadius * 0.6f,
                                   juce::Colour (0xff15161a), centre.x + bodyRadius * 0.5f, centre.y + bodyRadius, true);
        g.setGradientFill (grad);
        g.fillEllipse (bodyRect);

        // Rim
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), bodyRect.getTopLeft(),
                                                 juce::Colours::black.withAlpha (0.5f), bodyRect.getBottomRight(), false));
        g.drawEllipse (bodyRect.reduced (0.5f), 1.2f);

        // Machined inner cap
        const auto cap = bodyRect.reduced (bodyRadius * 0.22f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f36), cap.getX(), cap.getY(),
                                                 juce::Colour (0xff1b1d22), cap.getX(), cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (cap, 1.0f);
    }

    // --- Pointer
    {
        const float inner = bodyRadius * 0.28f;
        const float outer = bodyRadius * 0.86f;
        const juce::Point<float> a (centre.x + inner * std::sin (angle), centre.y - inner * std::cos (angle));
        const juce::Point<float> b (centre.x + outer * std::sin (angle), centre.y - outer * std::cos (angle));

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine ({ a.translated (0.0f, 1.0f), b.translated (0.0f, 1.0f) }, juce::jmax (2.0f, size * 0.045f));
        g.setColour (hover ? Colours::accentBright : Colours::text);
        g.drawLine ({ a, b }, juce::jmax (2.0f, size * 0.04f));
    }
}

//==============================================================================
void SwarmLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool hover = slider.isMouseOverOrDragging();
    const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);

    const float grooveW = 6.0f;
    const auto groove = bounds.withSizeKeepingCentre (grooveW, bounds.getHeight());

    // Scale ticks
    g.setColour (Colours::textFaint.withAlpha (0.6f));
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = juce::jmap ((float) i, 0.0f, 10.0f, maxSliderPos, minSliderPos);
        const float len = (i % 5 == 0) ? 7.0f : 4.0f;
        g.drawHorizontalLine ((int) ty, groove.getX() - 6.0f - len, groove.getX() - 6.0f);
        g.drawHorizontalLine ((int) ty, groove.getRight() + 6.0f, groove.getRight() + 6.0f + len);
    }

    // Groove
    g.setColour (Colours::inset);
    g.fillRoundedRectangle (groove, grooveW * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (groove, grooveW * 0.5f, 1.0f);

    // Fill
    float fillFrom = minSliderPos;
    if (bipolar)
    {
        const double zeroProp = slider.valueToProportionOfLength (0.0);
        fillFrom = juce::jmap ((float) zeroProp, minSliderPos, maxSliderPos);
    }
    auto fill = groove.withTop (juce::jmin (fillFrom, sliderPos)).withBottom (juce::jmax (fillFrom, sliderPos));
    g.setColour (Colours::accent.withAlpha (0.2f));
    g.fillRoundedRectangle (fill.expanded (3.0f, 0.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (Colours::accentBright, 0.0f, fill.getY(),
                                             Colours::accentDeep, 0.0f, fill.getBottom(), false));
    g.fillRoundedRectangle (fill, grooveW * 0.5f);

    // Thumb (fader cap)
    const float capW = juce::jmin (bounds.getWidth() - 4.0f, 34.0f);
    const float capH = 20.0f;
    const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ bounds.getCentreX(), sliderPos });

    juce::Path capPath; capPath.addRoundedRectangle (cap, 3.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 8, { 0, 3 }).drawForPath (g, capPath);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e57), cap.getX(), cap.getY(),
                                             juce::Colour (0xff1d1f24), cap.getX(), cap.getBottom(), false));
    g.fillPath (capPath);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine ((int) cap.getY() + 1, cap.getX() + 2.0f, cap.getRight() - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);

    // grip lines + indicator
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawHorizontalLine ((int) cap.getCentreY() - 4, cap.getX() + 5.0f, cap.getRight() - 5.0f);
    g.drawHorizontalLine ((int) cap.getCentreY() + 4, cap.getX() + 5.0f, cap.getRight() - 5.0f);
    g.setColour (hover ? Colours::accentBright : Colours::accent);
    g.fillRect (cap.withSizeKeepingCentre (capW - 8.0f, 2.0f));
}

//==============================================================================
void SwarmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool isMouseOver, bool isDown)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    auto base = on ? Colours::accent.withAlpha (0.22f) : Colours::panelHi;
    if (isDown)           base = base.darker (0.2f);
    else if (isMouseOver) base = base.brighter (0.12f);

    g.setGradientFill (juce::ColourGradient (base.brighter (0.05f), r.getX(), r.getY(),
                                             base.darker (0.15f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? Colours::accent.withAlpha (0.8f) : (isMouseOver ? Colours::textFaint : Colours::panelBorder));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font SwarmLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (16.0f, buttonHeight * 0.62f), true);
}

void SwarmLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOver, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    const bool on = button.getToggleState();
    g.setColour ((on || isMouseOver) ? Colours::accentBright : Colours::text.withAlpha (button.isEnabled() ? 0.9f : 0.4f));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, false);
}

//==============================================================================
void SwarmLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::panelBorder);
    g.drawRect (0, 0, width, height);
}

juce::Font SwarmLookAndFeel::getPopupMenuFont()
{
    return font (17.0f);
}

void SwarmLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName)
{
    g.setFont (font (14.0f, true));
    g.setColour (Colours::accent);
    g.drawText (sectionName.toUpperCase(), area.reduced (12, 0), juce::Justification::bottomLeft, true);
}

void SwarmLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                          bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                          const juce::String& text, const juce::String& shortcutKeyText,
                                          const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (Colours::panelBorder);
        g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
        return;
    }

    auto r = area.reduced (2, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (Colours::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
    }

    auto textArea = r.reduced (10, 0);
    if (isTicked)
    {
        g.setColour (Colours::accent);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) textArea.getX() + 3.0f, (float) r.getCentreY() }));
    }
    textArea.removeFromLeft (12);

    g.setFont (getPopupMenuFont());
    g.setColour (! isActive ? Colours::textFaint : (isHighlighted || isTicked ? Colours::accentBright : Colours::text));
    g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);

    if (hasSubMenu)
    {
        juce::Path arrow;
        const float ax = (float) r.getRight() - 12.0f, ay = (float) r.getCentreY();
        arrow.addTriangle (ax - 3.0f, ay - 4.0f, ax - 3.0f, ay + 4.0f, ax + 2.0f, ay);
        g.fillPath (arrow);
    }
    else if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (font (14.0f));
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }
}

//==============================================================================
void SwarmLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (Colours::panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (font (15.0f));
    g.drawFittedText (text, r.toNearestInt().reduced (8, 4), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> SwarmLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    const auto f = font (15.0f);
    const int w = juce::jmin (320, (int) juce::GlyphArrangement::getStringWidth (f, tipText) + 20);
    const int lines = 1 + (int) (juce::GlyphArrangement::getStringWidth (f, tipText) / 300.0f);
    const int h = 10 + lines * 18;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

juce::Font SwarmLookAndFeel::getAlertWindowTitleFont()   { return font (20.0f, true); }
juce::Font SwarmLookAndFeel::getAlertWindowMessageFont() { return font (16.0f); }
juce::Font SwarmLookAndFeel::getAlertWindowFont()        { return font (16.0f); }
juce::Font SwarmLookAndFeel::getLabelFont (juce::Label& l)  { return font (juce::jmax (12.0f, l.getFont().getHeight())); }

void SwarmLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (Colours::inset);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
}

void SwarmLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te)
{
    g.setColour (te.hasKeyboardFocus (true) ? Colours::accent : Colours::panelBorder);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 4.0f, 1.0f);
}
