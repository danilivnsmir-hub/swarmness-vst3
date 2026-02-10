#include "MetalLookAndFeel.h"

MetalLookAndFeel::MetalLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, getBackgroundDark());
    setColour(juce::Label::textColourId, getTextLight());
    setColour(juce::Slider::textBoxTextColourId, getTextLight());
    setColour(juce::Slider::textBoxOutlineColourId, getMetalGrey());
    setColour(juce::ComboBox::backgroundColourId, getPanelBackground());
    setColour(juce::ComboBox::textColourId, getTextLight());
    setColour(juce::ComboBox::arrowColourId, getTextLight());
    setColour(juce::ComboBox::outlineColourId, getMetalDark());
    setColour(juce::PopupMenu::backgroundColourId, getPanelBackground());
    setColour(juce::PopupMenu::textColourId, getTextLight());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, getAccentOrange());
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour(juce::TextButton::buttonColourId, getMetalDark());
    setColour(juce::TextButton::textColourOffId, getTextLight());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void MetalLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider) {
    const float radius = juce::jmin(width / 2.0f, height / 2.0f) - 6.0f;
    const float centreX = x + width * 0.5f;
    const float centreY = y + height * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // === OUTER HEXAGONAL BEZEL ===
    {
        juce::Path hexPath;
        const float hexRadius = radius + 4.0f;
        for (int i = 0; i < 6; i++) {
            float a = juce::MathConstants<float>::pi / 6.0f + i * juce::MathConstants<float>::pi / 3.0f;
            float hx = centreX + hexRadius * std::cos(a);
            float hy = centreY + hexRadius * std::sin(a);
            if (i == 0) hexPath.startNewSubPath(hx, hy);
            else hexPath.lineTo(hx, hy);
        }
        hexPath.closeSubPath();
        
        // Hexagon gradient (brushed metal)
        juce::ColourGradient hexGrad(getMetalMid(), centreX - hexRadius, centreY - hexRadius,
                                      getMetalDark(), centreX + hexRadius, centreY + hexRadius, false);
        g.setGradientFill(hexGrad);
        g.fillPath(hexPath);
        
        // Hex border
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.strokePath(hexPath, juce::PathStrokeType(1.5f));
    }

    // === ORANGE ARC INDICATOR (behind knob) ===
    const float arcRadius = radius - 1.0f;
    const float arcThickness = 3.5f;
    
    // Background arc (dim orange)
    {
        juce::Path bgArc;
        bgArc.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(getAccentOrangeDim());
        g.strokePath(bgArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Value arc (bright orange)
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                               0.0f, rotaryStartAngle, angle, true);
        g.setColour(getAccentOrangeBright());
        g.strokePath(valueArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // === OUTER RING (grooved edge) ===
    const float outerRingRadius = radius - 5.0f;
    {
        juce::ColourGradient ringGrad(getMetalLight(), centreX, centreY - outerRingRadius,
                                       getMetalGrey(), centreX, centreY + outerRingRadius, false);
        g.setGradientFill(ringGrad);
        g.fillEllipse(centreX - outerRingRadius, centreY - outerRingRadius, 
                      outerRingRadius * 2.0f, outerRingRadius * 2.0f);
        
        // Inner shadow for groove
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.drawEllipse(centreX - outerRingRadius + 1, centreY - outerRingRadius + 1,
                      outerRingRadius * 2.0f - 2, outerRingRadius * 2.0f - 2, 2.0f);
    }

    // === INNER GROOVE RING ===
    const float innerRingRadius = radius - 10.0f;
    {
        g.setColour(getMetalDark());
        g.fillEllipse(centreX - innerRingRadius, centreY - innerRingRadius,
                      innerRingRadius * 2.0f, innerRingRadius * 2.0f);
    }

    // === CENTER CAP (main brushed metal surface) ===
    const float capRadius = radius - 14.0f;
    {
        // Main gradient (vertical brushed metal)
        juce::ColourGradient capGrad(getMetalLight().brighter(0.2f), centreX, centreY - capRadius * 0.8f,
                                      getMetalGrey(), centreX, centreY + capRadius * 0.8f, false);
        g.setGradientFill(capGrad);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2.0f, capRadius * 2.0f);
        
        // Highlight arc (top)
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        juce::Path highlightArc;
        highlightArc.addCentredArc(centreX, centreY, capRadius - 2, capRadius - 2,
                                   0.0f, -2.5f, -0.5f, true);
        g.strokePath(highlightArc, juce::PathStrokeType(2.0f));
        
        // Subtle border
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);
    }

    // === POINTER LINE ===
    {
        juce::Path pointerPath;
        const float pointerLength = capRadius * 0.7f;
        const float pointerWidth = 2.5f;
        
        pointerPath.addRoundedRectangle(-pointerWidth * 0.5f, -capRadius + 4.0f, 
                                         pointerWidth, pointerLength, pointerWidth * 0.4f);
        
        g.setColour(getTextLight());
        g.fillPath(pointerPath, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    }

    // === SMALL CENTER DOT ===
    {
        g.setColour(getMetalDark());
        g.fillEllipse(centreX - 3.0f, centreY - 3.0f, 6.0f, 6.0f);
    }
}

void MetalLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    juce::Colour baseColour = shouldDrawButtonAsDown ? getAccentOrange().darker(0.2f) :
                              shouldDrawButtonAsHighlighted ? getMetalGrey() : getMetalDark();

    if (button.getToggleState())
        baseColour = getAccentOrange();

    // Draw with gradient for depth
    juce::ColourGradient grad(baseColour.brighter(0.1f), bounds.getX(), bounds.getY(),
                               baseColour.darker(0.2f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void MetalLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
                                     bool isButtonDown, int buttonX, int buttonY,
                                     int buttonW, int buttonH, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    
    // Dark background with gradient
    juce::ColourGradient grad(getPanelBackground().brighter(0.1f), 0, 0,
                               getPanelBackground().darker(0.1f), 0, height, false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 3.0f);
    
    // Orange accent border
    g.setColour(getAccentOrangeDim());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    // Arrow (orange)
    juce::Path arrow;
    float arrowSize = 5.0f;
    float arrowX = width - 12.0f;
    float arrowY = height * 0.5f;
    arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.4f,
                      arrowX + arrowSize, arrowY - arrowSize * 0.4f,
                      arrowX, arrowY + arrowSize * 0.6f);
    g.setColour(getAccentOrangeBright());
    g.fillPath(arrow);
}

void MetalLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(getPanelBackground());
    g.setColour(getAccentOrangeDim());
    g.drawRect(0, 0, width, height, 1);
}

void MetalLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool hasSubMenu,
                                          const juce::String& text, const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon, const juce::Colour* textColour) {
    if (isSeparator) {
        auto r = area.reduced(5, 0).toFloat();
        r.removeFromTop(r.getHeight() * 0.5f - 0.5f);
        g.setColour(getMetalDark());
        g.fillRect(r.removeFromTop(1.0f));
        return;
    }

    auto r = area.reduced(1);

    if (isHighlighted && isActive) {
        g.setColour(getAccentOrange());
        g.fillRect(r);
        g.setColour(juce::Colours::black);
    } else {
        g.setColour(isActive ? getTextLight() : getTextDim());
    }

    auto font = juce::Font(13.0f);
    g.setFont(font);

    auto textArea = r.reduced(10, 0);
    g.drawText(text, textArea, juce::Justification::centredLeft, true);
}

void MetalLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited()) {
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(
            label.isEnabled() ? 1.0f : 0.5f));
        g.setFont(label.getFont());
        g.drawText(label.getText(), label.getLocalBounds(),
                   label.getJustificationType(), true);
    }
}

void MetalLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat();
    const float cornerSize = 12.0f;  // More rounded for pill shape
    const float padding = 2.0f;
    
    // Outer pill shape - dark background
    juce::Colour bgColour = juce::Colour(0xff1a1a1a);
    if (shouldDrawButtonAsHighlighted)
        bgColour = bgColour.brighter(0.05f);
    
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.reduced(1), cornerSize);
    
    // Border with orange glow when ON
    if (button.getToggleState()) {
        g.setColour(getAccentOrange().withAlpha(0.6f));
        g.drawRoundedRectangle(bounds.reduced(1), cornerSize, 2.0f);
    } else {
        g.setColour(getMetalDark());
        g.drawRoundedRectangle(bounds.reduced(1), cornerSize, 1.0f);
    }
    
    // Thumb circle position
    float thumbDiameter = bounds.getHeight() - padding * 4;
    float thumbX = button.getToggleState() 
        ? bounds.getRight() - thumbDiameter - padding * 3
        : bounds.getX() + padding * 3;
    float thumbY = bounds.getCentreY() - thumbDiameter * 0.5f;
    
    // Orange glow bar when ON
    if (button.getToggleState()) {
        auto glowBounds = bounds.reduced(padding * 2);
        g.setColour(getAccentOrange());
        g.fillRoundedRectangle(glowBounds.getX() + thumbDiameter, 
                               glowBounds.getCentreY() - 2,
                               glowBounds.getWidth() - thumbDiameter * 2, 
                               4.0f, 2.0f);
    }
    
    // Thumb circle
    g.setColour(getMetalGrey());
    g.fillEllipse(thumbX, thumbY, thumbDiameter, thumbDiameter);
    
    // Thumb highlight
    g.setColour(getMetalLight().withAlpha(0.3f));
    g.drawEllipse(thumbX + 1, thumbY + 1, thumbDiameter - 2, thumbDiameter - 2, 1.0f);
}

void MetalLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider) {
    // Only handle vertical sliders with custom styling
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearBarVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    
    const float trackWidth = 20.0f;
    const float trackX = x + (width - trackWidth) * 0.5f;
    const float trackHeight = static_cast<float>(height) - 20.0f;
    const float trackY = static_cast<float>(y) + 10.0f;
    const float cornerRadius = 10.0f;
    
    // Track background (dark)
    juce::Rectangle<float> trackBounds(trackX, trackY, trackWidth, trackHeight);
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRoundedRectangle(trackBounds, cornerRadius);
    
    // Track border
    g.setColour(getMetalDark());
    g.drawRoundedRectangle(trackBounds, cornerRadius, 1.5f);
    
    // Calculate fill based on slider position (inverted for vertical)
    float normalizedPos = 1.0f - (sliderPos - y) / static_cast<float>(height);
    normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
    float fillHeight = trackHeight * normalizedPos;
    
    // Orange fill from bottom
    if (fillHeight > 0) {
        juce::Rectangle<float> fillBounds(trackX + 2, trackY + trackHeight - fillHeight, 
                                           trackWidth - 4, fillHeight);
        
        // Gradient fill for glow effect
        juce::ColourGradient fillGrad(getAccentOrangeBright(), trackX, fillBounds.getY(),
                                       getAccentOrange().darker(0.3f), trackX + trackWidth, fillBounds.getY(), false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(fillBounds, cornerRadius - 2);
        
        // Glow effect
        g.setColour(getAccentOrange().withAlpha(0.3f));
        g.drawRoundedRectangle(fillBounds.expanded(1), cornerRadius - 1, 2.0f);
    }
    
    // Thumb/handle
    const float thumbHeight = 20.0f;
    const float thumbWidth = trackWidth + 8.0f;
    const float thumbX = trackX - 4.0f;
    const float thumbY = sliderPos - thumbHeight * 0.5f;
    
    // Thumb shadow
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(thumbX + 1, thumbY + 1, thumbWidth, thumbHeight, 4.0f);
    
    // Thumb body - metal gradient
    juce::ColourGradient thumbGrad(getMetalLight(), thumbX, thumbY,
                                    getMetalGrey(), thumbX, thumbY + thumbHeight, false);
    g.setGradientFill(thumbGrad);
    g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);
    
    // Thumb border
    g.setColour(getMetalDark());
    g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f, 1.0f);
    
    // Thumb grip lines
    g.setColour(getMetalDark().withAlpha(0.5f));
    for (int i = -1; i <= 1; ++i) {
        float lineY = thumbY + thumbHeight * 0.5f + i * 4.0f;
        g.drawLine(thumbX + 4, lineY, thumbX + thumbWidth - 4, lineY, 1.0f);
    }
}
