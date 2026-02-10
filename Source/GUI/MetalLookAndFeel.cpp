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
    
    // v1.0.0: Load image assets for controls
    knobImage = juce::ImageCache::getFromMemory(BinaryData::knob_png, BinaryData::knob_pngSize);
    toggleImage = juce::ImageCache::getFromMemory(BinaryData::toggle_png, BinaryData::toggle_pngSize);
    faderImage = juce::ImageCache::getFromMemory(BinaryData::fader_png, BinaryData::fader_pngSize);
}

void MetalLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider) {
    const float centreX = x + width * 0.5f;
    const float centreY = y + height * 0.5f;
    const float size = juce::jmin(width, height) * 0.9f;
    
    // v1.0.0: Use smoothed value for rotation if available
    float smoothedValue = slider.getProperties().getWithDefault("smoothedValue", -1.0f);
    float normalizedPos = sliderPos;
    if (smoothedValue >= 0.0f) {
        double range = slider.getMaximum() - slider.getMinimum();
        if (range > 0.0) {
            normalizedPos = static_cast<float>((smoothedValue - slider.getMinimum()) / range);
            normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        }
    }
    
    // v1.0.0: Calculate rotation angle (-135° to +135°, i.e. 270° range)
    const float startAngle = -juce::MathConstants<float>::pi * 0.75f;  // -135°
    const float endAngle = juce::MathConstants<float>::pi * 0.75f;     // +135°
    const float angle = startAngle + normalizedPos * (endAngle - startAngle);
    
    // v1.0.0: Check hover state for brightness boost
    bool isHovered = slider.getProperties().getWithDefault("isHovered", false);
    
    // v1.0.0: Image-based knob rendering
    if (knobImage.isValid()) {
        juce::Rectangle<float> destRect(centreX - size * 0.5f, centreY - size * 0.5f, size, size);
        
        // Apply rotation transform around center
        juce::AffineTransform transform = juce::AffineTransform::rotation(angle, centreX, centreY);
        
        g.saveState();
        g.addTransform(transform);
        
        // Draw knob image with optional brightness for hover
        if (isHovered) {
            // Draw brighter version on hover
            g.drawImage(knobImage, destRect.getX(), destRect.getY(), destRect.getWidth(), destRect.getHeight(),
                        0, 0, knobImage.getWidth(), knobImage.getHeight());
            g.setColour(juce::Colours::white.withAlpha(0.1f));
            g.fillEllipse(destRect.reduced(4));
        } else {
            g.drawImage(knobImage, destRect.getX(), destRect.getY(), destRect.getWidth(), destRect.getHeight(),
                        0, 0, knobImage.getWidth(), knobImage.getHeight());
        }
        
        g.restoreState();
    } else {
        // Fallback: simple circle if image not loaded
        g.setColour(getMetalGrey());
        g.fillEllipse(centreX - size * 0.4f, centreY - size * 0.4f, size * 0.8f, size * 0.8f);
        
        // Pointer line
        juce::Path pointer;
        pointer.addRectangle(-2.0f, -size * 0.35f, 4.0f, size * 0.2f);
        g.setColour(getAccentOrange());
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
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
    
    // v1.0.0: Image-based toggle rendering
    // toggle.png is a strip: left half = OFF, right half = ON
    if (toggleImage.isValid()) {
        int imgWidth = toggleImage.getWidth();
        int imgHeight = toggleImage.getHeight();
        int halfWidth = imgWidth / 2;
        
        // Select which half to draw based on state
        int srcX = button.getToggleState() ? halfWidth : 0;
        
        g.drawImage(toggleImage, 
                    bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                    srcX, 0, halfWidth, imgHeight);
    } else {
        // Fallback: simple toggle if image not loaded
        const float cornerSize = 4.0f;
        juce::Rectangle<float> trackBounds = bounds.reduced(1);
        
        // Background
        g.setColour(button.getToggleState() ? getAccentOrange().darker(0.3f) : getMetalDark());
        g.fillRoundedRectangle(trackBounds, cornerSize);
        
        // Border
        g.setColour(button.getToggleState() ? getAccentOrange() : getMetalGrey());
        g.drawRoundedRectangle(trackBounds, cornerSize, 1.0f);
        
        // Thumb
        float thumbWidth = bounds.getWidth() * 0.45f;
        float thumbHeight = bounds.getHeight() - 8.0f;
        float thumbX = button.getToggleState() 
            ? bounds.getRight() - thumbWidth - 4.0f
            : bounds.getX() + 4.0f;
        float thumbY = bounds.getCentreY() - thumbHeight * 0.5f;
        
        g.setColour(getMetalLight());
        g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, cornerSize);
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
    
    // v1.0.0: Image-based fader rendering
    // fader.png is 1024x1536 - left half = empty track, right half = filled track
    if (faderImage.isValid()) {
        int imgWidth = faderImage.getWidth();
        int imgHeight = faderImage.getHeight();
        int halfWidth = imgWidth / 2;
        
        // Calculate normalized position (inverted for vertical - 0 at bottom, 1 at top)
        float normalizedPos = 1.0f - (sliderPos - static_cast<float>(y)) / static_cast<float>(height);
        normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        
        // Draw the empty track (left half of image)
        g.drawImage(faderImage, 
                    static_cast<float>(x), static_cast<float>(y), 
                    static_cast<float>(width), static_cast<float>(height),
                    0, 0, halfWidth, imgHeight);
        
        // Calculate fill amount from bottom
        int fillPixels = static_cast<int>(imgHeight * normalizedPos);
        int destFillPixels = static_cast<int>(height * normalizedPos);
        
        if (fillPixels > 0 && destFillPixels > 0) {
            // Draw filled portion from right half, starting from bottom
            int srcY = imgHeight - fillPixels;
            int destY = y + height - destFillPixels;
            
            g.drawImage(faderImage,
                        static_cast<float>(x), static_cast<float>(destY),
                        static_cast<float>(width), static_cast<float>(destFillPixels),
                        halfWidth, srcY, halfWidth, fillPixels);
        }
    } else {
        // Fallback: simple vertical slider
        const float trackWidth = 22.0f;
        const float trackX = static_cast<float>(x) + (static_cast<float>(width) - trackWidth) * 0.5f;
        const float trackHeight = static_cast<float>(height);
        const float trackY = static_cast<float>(y);
        
        // Track background
        g.setColour(getMetalDark());
        g.fillRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 6.0f);
        
        // Calculate fill
        float normalizedPos = 1.0f - (sliderPos - static_cast<float>(y)) / static_cast<float>(height);
        normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        float fillHeight = trackHeight * normalizedPos;
        
        // Orange fill from bottom
        if (fillHeight > 2.0f) {
            g.setColour(getAccentOrange());
            g.fillRoundedRectangle(trackX + 2, trackY + trackHeight - fillHeight, 
                                   trackWidth - 4, fillHeight, 4.0f);
        }
        
        // Thumb
        const float thumbHeight = 20.0f;
        const float thumbWidth = trackWidth + 10.0f;
        const float thumbX = trackX - 5.0f;
        const float thumbY = juce::jlimit(static_cast<float>(y), 
                                          static_cast<float>(y + height) - thumbHeight, 
                                          sliderPos - thumbHeight * 0.5f);
        
        g.setColour(getMetalLight());
        g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);
        g.setColour(getMetalDark());
        g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f, 1.0f);
    }
}
