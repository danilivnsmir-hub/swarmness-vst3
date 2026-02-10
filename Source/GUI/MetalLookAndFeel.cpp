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
    
    // v1.2.0: Load image assets for controls (optional fallback)
    knobImage = juce::ImageCache::getFromMemory(BinaryData::knob_png, BinaryData::knob_pngSize);
    toggleImage = juce::ImageCache::getFromMemory(BinaryData::toggle_png, BinaryData::toggle_pngSize);
    faderImage = juce::ImageCache::getFromMemory(BinaryData::fader_png, BinaryData::fader_pngSize);
}

void MetalLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider) {
    const float centreX = x + width * 0.5f;
    const float centreY = y + height * 0.5f;
    const float radius = static_cast<float>(juce::jmin(width, height)) * 0.4f;
    
    // v1.2.0: Calculate rotation angle
    const float startAngle = -juce::MathConstants<float>::pi * 0.75f;  // -135°
    const float endAngle = juce::MathConstants<float>::pi * 0.75f;     // +135°
    
    // Use smoothed value if available
    float smoothedValue = slider.getProperties().getWithDefault("smoothedValue", -1.0f);
    float normalizedPos = sliderPos;
    if (smoothedValue >= 0.0f) {
        double range = slider.getMaximum() - slider.getMinimum();
        if (range > 0.0) {
            normalizedPos = static_cast<float>((smoothedValue - slider.getMinimum()) / range);
            normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        }
    }
    
    const float angle = startAngle + normalizedPos * (endAngle - startAngle);
    
    // v1.2.0: Draw tick marks around the knob
    g.setColour(juce::Colour(0xff444444));
    const float tickRadius = radius + 6.0f;
    const int numTicks = 11;
    for (int i = 0; i <= numTicks; i++) {
        float tickAngle = startAngle + i * (endAngle - startAngle) / static_cast<float>(numTicks);
        float tickLength = (i % 5 == 0) ? 6.0f : 4.0f;  // Major ticks longer
        float innerR = tickRadius - tickLength;
        float outerR = tickRadius;
        
        float x1 = centreX + innerR * std::sin(tickAngle);
        float y1 = centreY - innerR * std::cos(tickAngle);
        float x2 = centreX + outerR * std::sin(tickAngle);
        float y2 = centreY - outerR * std::cos(tickAngle);
        
        g.drawLine(x1, y1, x2, y2, 1.0f);
    }
    
    // v1.2.0: Dark gray knob body (#2A2A2A)
    g.setColour(juce::Colour(0xff2A2A2A));
    g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
    
    // Inner shadow
    juce::ColourGradient shadowGrad(juce::Colour(0x40000000), centreX, centreY - radius,
                                     juce::Colours::transparentBlack, centreX, centreY, true);
    g.setGradientFill(shadowGrad);
    g.fillEllipse(centreX - radius + 2, centreY - radius + 2, (radius - 2) * 2.0f, (radius - 2) * 2.0f);
    
    // Subtle edge highlight
    g.setColour(juce::Colour(0x20FFFFFF));
    g.drawEllipse(centreX - radius + 1, centreY - radius + 1, (radius - 1) * 2.0f, (radius - 1) * 2.0f, 1.0f);
    
    // v1.2.1: Arc fill showing parameter value
    if (normalizedPos > 0.01f) {
        const float arcRadius = radius + 4.0f;
        const float arcThickness = 3.0f;
        float fillAngle = startAngle + normalizedPos * (endAngle - startAngle);
        
        juce::Path arcPath;
        arcPath.addArc(centreX - arcRadius, centreY - arcRadius, 
                       arcRadius * 2.0f, arcRadius * 2.0f,
                       startAngle, fillAngle, true);
        
        g.setColour(getAccentOrange());
        g.strokePath(arcPath, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // v1.2.0: Orange indicator line from center
    g.setColour(getAccentOrange());
    const float indicatorLength = radius * 0.65f;
    const float indicatorWidth = 3.0f;
    
    juce::Path indicator;
    indicator.addRoundedRectangle(-indicatorWidth * 0.5f, -radius + 4.0f, indicatorWidth, indicatorLength, 1.5f);
    
    g.fillPath(indicator, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    
    // Center cap
    g.setColour(juce::Colour(0xff1A1A1A));
    g.fillEllipse(centreX - 5.0f, centreY - 5.0f, 10.0f, 10.0f);
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
    float arrowSize = 4.0f;
    float arrowX = width - 10.0f;
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

    auto font = juce::Font(12.0f);
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
    
    // v1.2.0: Minimal toggle button style
    bool isOn = button.getToggleState();
    
    // Background pill shape
    float cornerSize = bounds.getHeight() * 0.5f;
    g.setColour(isOn ? getAccentOrange().darker(0.3f) : juce::Colour(0xff333333));
    g.fillRoundedRectangle(bounds.reduced(2), cornerSize);
    
    // Border
    g.setColour(isOn ? getAccentOrange() : juce::Colour(0xff444444));
    g.drawRoundedRectangle(bounds.reduced(2), cornerSize, 1.5f);
    
    // Toggle indicator (circle that moves)
    float thumbDiameter = bounds.getHeight() - 8.0f;
    float thumbX = isOn ? bounds.getRight() - thumbDiameter - 5.0f : bounds.getX() + 5.0f;
    float thumbY = bounds.getCentreY() - thumbDiameter * 0.5f;
    
    g.setColour(isOn ? getAccentOrange() : juce::Colour(0xff666666));
    g.fillEllipse(thumbX, thumbY, thumbDiameter, thumbDiameter);
    
    // Subtle highlight on thumb
    if (isOn) {
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.fillEllipse(thumbX + 2, thumbY + 2, thumbDiameter - 4, (thumbDiameter - 4) * 0.5f);
    }
}

void MetalLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider) {
    // v1.2.4: Handle horizontal sliders
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar) {
        const float trackHeight = 8.0f;
        const float trackX = static_cast<float>(x) + 4.0f;
        const float trackY = static_cast<float>(y) + (static_cast<float>(height) - trackHeight) * 0.5f;
        const float trackWidth = static_cast<float>(width) - 8.0f;
        
        // Dark track background (#1A1A1A)
        g.setColour(juce::Colour(0xff1A1A1A));
        g.fillRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 4.0f);
        
        // Track border (#333333)
        g.setColour(juce::Colour(0xff333333));
        g.drawRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 4.0f, 1.0f);
        
        // Calculate normalized position (0 at left, 1 at right)
        float normalizedPos = (sliderPos - static_cast<float>(x)) / static_cast<float>(width);
        normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        
        // v1.2.4: Orange fill from left (#FF9500)
        float fillWidth = (trackWidth - 4.0f) * normalizedPos;
        if (fillWidth > 2.0f) {
            // Gradient fill for depth
            juce::ColourGradient fillGrad(getAccentOrangeBright(), trackX + 2.0f, trackY,
                                           getAccentOrange().darker(0.2f), trackX + 2.0f + fillWidth, trackY, false);
            g.setGradientFill(fillGrad);
            g.fillRoundedRectangle(trackX + 2.0f, trackY + 2.0f, fillWidth, trackHeight - 4.0f, 2.0f);
        }
        
        // v1.2.4: Minimal thumb (small circle)
        const float thumbDiameter = 14.0f;
        const float thumbX = juce::jlimit(static_cast<float>(x) + 2.0f, 
                                          static_cast<float>(x + width) - thumbDiameter - 2.0f, 
                                          sliderPos - thumbDiameter * 0.5f);
        const float thumbY = static_cast<float>(y) + (static_cast<float>(height) - thumbDiameter) * 0.5f;
        
        // Thumb background
        g.setColour(juce::Colour(0xff3A3A3A));
        g.fillEllipse(thumbX, thumbY, thumbDiameter, thumbDiameter);
        
        // Thumb highlight
        g.setColour(juce::Colour(0xff4A4A4A));
        g.fillEllipse(thumbX + 2, thumbY + 2, thumbDiameter - 4, (thumbDiameter - 4) * 0.6f);
        
        // Orange accent on thumb
        g.setColour(getAccentOrange());
        g.fillEllipse(thumbX + 4, thumbY + 4, thumbDiameter - 8, thumbDiameter - 8);
        
        return;
    }
    
    // Handle other slider types (non-vertical, non-horizontal) with default
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearBarVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    
    // v1.2.0: Minimal vertical fader style
    const float trackWidth = 16.0f;
    const float trackX = static_cast<float>(x) + (static_cast<float>(width) - trackWidth) * 0.5f;
    const float trackY = static_cast<float>(y) + 4.0f;
    const float trackHeight = static_cast<float>(height) - 8.0f;
    
    // Dark track background (#1A1A1A)
    g.setColour(juce::Colour(0xff1A1A1A));
    g.fillRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 4.0f);
    
    // Track border (#333333)
    g.setColour(juce::Colour(0xff333333));
    g.drawRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 4.0f, 1.0f);
    
    // Calculate normalized position (inverted for vertical - 0 at bottom, 1 at top)
    float normalizedPos = 1.0f - (sliderPos - static_cast<float>(y)) / static_cast<float>(height);
    normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
    
    // v1.2.0: Orange fill from bottom (#FF9500)
    float fillHeight = (trackHeight - 4.0f) * normalizedPos;
    if (fillHeight > 2.0f) {
        float fillY = trackY + trackHeight - 2.0f - fillHeight;
        
        // Gradient fill for depth
        juce::ColourGradient fillGrad(getAccentOrangeBright(), trackX + trackWidth * 0.5f, fillY,
                                       getAccentOrange().darker(0.2f), trackX + trackWidth * 0.5f, fillY + fillHeight, false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(trackX + 2.0f, fillY, trackWidth - 4.0f, fillHeight, 2.0f);
    }
    
    // v1.2.0: Minimal thumb (small rectangle)
    const float thumbHeight = 14.0f;
    const float thumbWidth = trackWidth + 8.0f;
    const float thumbX = trackX - 4.0f;
    const float thumbY = juce::jlimit(static_cast<float>(y) + 2.0f, 
                                      static_cast<float>(y + height) - thumbHeight - 2.0f, 
                                      sliderPos - thumbHeight * 0.5f);
    
    // Thumb background
    g.setColour(juce::Colour(0xff3A3A3A));
    g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 3.0f);
    
    // Thumb highlight
    g.setColour(juce::Colour(0xff4A4A4A));
    g.fillRoundedRectangle(thumbX + 1, thumbY + 1, thumbWidth - 2, thumbHeight * 0.4f, 2.0f);
    
    // Thumb grip lines
    g.setColour(juce::Colour(0xff2A2A2A));
    float lineY = thumbY + thumbHeight * 0.5f;
    g.drawLine(thumbX + 4.0f, lineY - 2.0f, thumbX + thumbWidth - 4.0f, lineY - 2.0f, 1.0f);
    g.drawLine(thumbX + 4.0f, lineY + 2.0f, thumbX + thumbWidth - 4.0f, lineY + 2.0f, 1.0f);
}
