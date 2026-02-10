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
    
    // Phase 3 UI: Use smoothed value for arc animation if available
    float smoothedValue = slider.getProperties().getWithDefault("smoothedValue", -1.0f);
    float normalizedPos = sliderPos;
    if (smoothedValue >= 0.0f) {
        // Convert smoothed value to normalized position
        double range = slider.getMaximum() - slider.getMinimum();
        if (range > 0.0) {
            normalizedPos = static_cast<float>((smoothedValue - slider.getMinimum()) / range);
            normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        }
    }
    const float angle = rotaryStartAngle + normalizedPos * (rotaryEndAngle - rotaryStartAngle);
    
    // Phase 3 UI: Check hover state for brightness boost
    bool isHovered = slider.getProperties().getWithDefault("isHovered", false);
    float hoverBrightness = isHovered ? 0.08f : 0.0f;  // +8% brightness on hover

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
        
        // Hexagon gradient (brushed metal) - Phase 3 UI: brighter on hover
        juce::ColourGradient hexGrad(getMetalMid().brighter(hoverBrightness), centreX - hexRadius, centreY - hexRadius,
                                      getMetalDark().brighter(hoverBrightness), centreX + hexRadius, centreY + hexRadius, false);
        g.setGradientFill(hexGrad);
        g.fillPath(hexPath);
        
        // Hex border - Phase 3 UI: add orange tint on hover
        if (isHovered) {
            g.setColour(getAccentOrange().withAlpha(0.3f));
            g.strokePath(hexPath, juce::PathStrokeType(2.0f));
        }
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
    
    // Value arc (bright orange with GLOW effect)
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                               0.0f, rotaryStartAngle, angle, true);
        
        // Phase 2 UI: Outer glow layer (larger, more diffuse)
        g.setColour(getAccentOrange().withAlpha(0.25f));
        g.strokePath(valueArc, juce::PathStrokeType(arcThickness + 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        
        // Phase 2 UI: Middle glow layer
        g.setColour(getAccentOrange().withAlpha(0.4f));
        g.strokePath(valueArc, juce::PathStrokeType(arcThickness + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        
        // Main bright arc
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
        const float pointerWidth = 3.0f;  // Phase 1 UI: Increased from 2.5px to 3px
        
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
    const float cornerSize = 4.0f;  // Phase 2 UI: More angular/industrial look
    const float padding = 2.0f;
    
    // Phase 2 UI: Track background with metal gradient
    juce::Rectangle<float> trackBounds = bounds.reduced(1);
    
    // Inner shadow for depth
    g.setColour(juce::Colour(0xff0a0a0a));
    g.fillRoundedRectangle(trackBounds.expanded(1), cornerSize + 1);
    
    // Metal gradient background
    juce::ColourGradient trackGrad(juce::Colour(0xff252525), trackBounds.getX(), trackBounds.getY(),
                                    juce::Colour(0xff151515), trackBounds.getX(), trackBounds.getBottom(), false);
    g.setGradientFill(trackGrad);
    g.fillRoundedRectangle(trackBounds, cornerSize);
    
    // Border with orange glow when ON
    if (button.getToggleState()) {
        // Phase 2 UI: Orange glow effect
        g.setColour(getAccentOrange().withAlpha(0.3f));
        g.drawRoundedRectangle(trackBounds.expanded(2), cornerSize + 2, 3.0f);
        g.setColour(getAccentOrange().withAlpha(0.7f));
        g.drawRoundedRectangle(trackBounds, cornerSize, 1.5f);
    } else {
        g.setColour(getMetalDark());
        g.drawRoundedRectangle(trackBounds, cornerSize, 1.0f);
    }
    
    // Thumb position - rectangular with rounded corners
    float thumbWidth = bounds.getWidth() * 0.45f;
    float thumbHeight = bounds.getHeight() - padding * 4;
    float thumbX = button.getToggleState() 
        ? bounds.getRight() - thumbWidth - padding * 2
        : bounds.getX() + padding * 2;
    float thumbY = bounds.getCentreY() - thumbHeight * 0.5f;
    
    // Phase 2 UI: Orange fill bar when ON (behind thumb)
    if (button.getToggleState()) {
        juce::Rectangle<float> fillBounds(bounds.getX() + padding * 3, 
                                           bounds.getCentreY() - 3,
                                           thumbX - bounds.getX() - padding * 3, 
                                           6.0f);
        // Glow
        g.setColour(getAccentOrange().withAlpha(0.4f));
        g.fillRoundedRectangle(fillBounds.expanded(2), 2.0f);
        // Fill
        g.setColour(getAccentOrangeBright());
        g.fillRoundedRectangle(fillBounds, 2.0f);
    }
    
    // Thumb shadow
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(thumbX + 1, thumbY + 1, thumbWidth, thumbHeight, cornerSize);
    
    // Thumb body - brushed metal gradient
    juce::ColourGradient thumbGrad(getMetalLight().brighter(0.1f), thumbX, thumbY,
                                    getMetalGrey().darker(0.1f), thumbX, thumbY + thumbHeight, false);
    g.setGradientFill(thumbGrad);
    g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, cornerSize);
    
    // Thumb highlight (top edge)
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawLine(thumbX + 3, thumbY + 1.5f, thumbX + thumbWidth - 3, thumbY + 1.5f, 1.0f);
    
    // Thumb border
    g.setColour(getMetalDark());
    g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, cornerSize, 1.0f);
    
    // Phase 2 UI: Grip lines on thumb
    g.setColour(getAccentOrange().withAlpha(button.getToggleState() ? 0.7f : 0.3f));
    float lineX = thumbX + thumbWidth * 0.5f;
    for (int i = -1; i <= 1; ++i) {
        float lineY = thumbY + thumbHeight * 0.5f + i * 3.0f;
        g.drawLine(lineX - 4, lineY, lineX + 4, lineY, 1.0f);
    }
}

void MetalLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider) {
    // Only handle vertical sliders with custom styling
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearBarVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    
    const float trackWidth = 22.0f;  // Phase 2 UI: Slightly narrower for cleaner look
    const float trackX = static_cast<float>(x) + (static_cast<float>(width) - trackWidth) * 0.5f;
    const float thumbMargin = 14.0f;  // Space at top and bottom for thumb
    const float trackHeight = static_cast<float>(height) - thumbMargin * 2.0f;
    const float trackY = static_cast<float>(y) + thumbMargin;
    const float cornerRadius = 6.0f;  // Phase 2 UI: Slightly less rounded
    
    // Track background (dark with gradient depth)
    juce::Rectangle<float> trackBounds(trackX, trackY, trackWidth, trackHeight);
    
    // Phase 2 UI: Outer shadow for depth
    g.setColour(juce::Colour(0xff080808));
    g.fillRoundedRectangle(trackBounds.expanded(2), cornerRadius + 2);
    
    // Phase 2 UI: Track gradient from dark to slightly lighter (depth effect)
    juce::ColourGradient trackGrad(juce::Colour(0xff1c1c1c), trackBounds.getCentreX(), trackBounds.getY(),
                                    juce::Colour(0xff0f0f0f), trackBounds.getCentreX(), trackBounds.getBottom(), false);
    g.setGradientFill(trackGrad);
    g.fillRoundedRectangle(trackBounds, cornerRadius);
    
    // Track border
    g.setColour(getMetalDark().brighter(0.1f));
    g.drawRoundedRectangle(trackBounds, cornerRadius, 1.0f);
    
    // Calculate fill based on slider position (inverted for vertical)
    float normalizedPos = 1.0f - (sliderPos - static_cast<float>(y)) / static_cast<float>(height);
    normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
    float fillHeight = trackHeight * normalizedPos;
    
    // Phase 2 UI: Orange fill from bottom with enhanced glow
    if (fillHeight > 2.0f) {
        juce::Rectangle<float> fillBounds(trackX + 3, trackY + trackHeight - fillHeight + 2, 
                                           trackWidth - 6, fillHeight - 4);
        
        // Phase 2 UI: Outer glow layer
        g.setColour(getAccentOrange().withAlpha(0.2f));
        g.fillRoundedRectangle(fillBounds.expanded(4), cornerRadius);
        
        // Phase 2 UI: Middle glow layer
        g.setColour(getAccentOrange().withAlpha(0.35f));
        g.fillRoundedRectangle(fillBounds.expanded(2), cornerRadius - 1);
        
        // Gradient fill (brighter at center for tube-like effect)
        juce::ColourGradient fillGrad(getAccentOrangeBright().brighter(0.2f), fillBounds.getCentreX(), fillBounds.getY(),
                                       getAccentOrange().darker(0.1f), fillBounds.getCentreX(), fillBounds.getBottom(), false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(fillBounds, cornerRadius - 2);
        
        // Phase 2 UI: Inner highlight for glossy effect
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillRoundedRectangle(fillBounds.getX() + 2, fillBounds.getY() + 1, 
                               fillBounds.getWidth() * 0.3f, fillBounds.getHeight() - 2, cornerRadius - 3);
    }
    
    // Phase 2 UI: Thumb/handle - wider with better metal texture
    const float thumbHeight = 20.0f;
    const float thumbWidth = trackWidth + 14.0f;
    const float thumbX = trackX - 7.0f;
    const float thumbY = juce::jlimit(static_cast<float>(y), 
                                       static_cast<float>(y + height) - thumbHeight, 
                                       sliderPos - thumbHeight * 0.5f);
    
    // Thumb shadow (larger, softer)
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(thumbX + 2, thumbY + 3, thumbWidth, thumbHeight, 5.0f);
    
    // Thumb body - brushed metal gradient with beveled edges
    juce::ColourGradient thumbGrad(getMetalLight().brighter(0.15f), thumbX, thumbY,
                                    getMetalGrey(), thumbX, thumbY + thumbHeight * 0.5f, false);
    thumbGrad.addColour(0.5, getMetalGrey());
    thumbGrad.addColour(1.0, getMetalGrey().darker(0.2f));
    g.setGradientFill(thumbGrad);
    g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 5.0f);
    
    // Phase 2 UI: Top bevel highlight
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.drawLine(thumbX + 5, thumbY + 2, thumbX + thumbWidth - 5, thumbY + 2, 1.5f);
    
    // Phase 2 UI: Left edge highlight for 3D effect
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawLine(thumbX + 2, thumbY + 4, thumbX + 2, thumbY + thumbHeight - 4, 1.0f);
    
    // Thumb border
    g.setColour(getMetalDark());
    g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 5.0f, 1.0f);
    
    // Phase 2 UI: Grip lines (orange accent, thicker)
    g.setColour(getAccentOrange().withAlpha(0.7f));
    for (int i = -1; i <= 1; ++i) {
        float lineY = thumbY + thumbHeight * 0.5f + i * 4.0f;
        g.drawLine(thumbX + 8, lineY, thumbX + thumbWidth - 8, lineY, 1.5f);
    }
}
