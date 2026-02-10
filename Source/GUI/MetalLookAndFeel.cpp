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
    // v1.0.1: Use full bounds for knob image (was 0.9f, causing small knobs)
    const float size = static_cast<float>(juce::jmin(width, height));
    
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
    
    // v1.0.1: Image-based knob rendering with high-quality scaling
    if (knobImage.isValid()) {
        juce::Rectangle<float> destRect(centreX - size * 0.5f, centreY - size * 0.5f, size, size);
        
        g.saveState();
        
        // v1.0.1: Apply rotation around the center of destination rect
        juce::AffineTransform transform = juce::AffineTransform::rotation(angle, centreX, centreY);
        g.addTransform(transform);
        
        // v1.0.1: Use high-quality interpolation for better scaling
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(knobImage, destRect, juce::RectanglePlacement::centred);
        
        // Hover brightness overlay
        if (isHovered) {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillEllipse(destRect.reduced(size * 0.1f));
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
    
    // v1.0.1: Image-based toggle rendering with aspect ratio preservation
    // toggle.png is a strip: left half = OFF, right half = ON
    if (toggleImage.isValid()) {
        int imgWidth = toggleImage.getWidth();
        int imgHeight = toggleImage.getHeight();
        int halfWidth = imgWidth / 2;
        
        // v1.0.1: Calculate aspect ratio of one state (halfWidth x imgHeight)
        float srcAspect = static_cast<float>(halfWidth) / static_cast<float>(imgHeight);
        
        // v1.0.1: Calculate destination size maintaining aspect ratio
        float destW, destH;
        float boundsAspect = bounds.getWidth() / bounds.getHeight();
        
        if (boundsAspect > srcAspect) {
            // Bounds are wider - fit by height
            destH = bounds.getHeight();
            destW = destH * srcAspect;
        } else {
            // Bounds are taller - fit by width
            destW = bounds.getWidth();
            destH = destW / srcAspect;
        }
        
        // v1.0.1: Center in bounds
        float destX = bounds.getX() + (bounds.getWidth() - destW) * 0.5f;
        float destY = bounds.getY() + (bounds.getHeight() - destH) * 0.5f;
        
        // Select which half to draw based on state
        int srcX = button.getToggleState() ? halfWidth : 0;
        
        // v1.0.1: High-quality scaling
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(toggleImage, 
                    destX, destY, destW, destH,
                    srcX, 0, halfWidth, imgHeight);
    } else {
        // Fallback: simple toggle if image not loaded
        const float cornerSize = bounds.getHeight() * 0.5f;  // Pill shape
        juce::Rectangle<float> trackBounds = bounds.reduced(1);
        
        // Background track
        g.setColour(button.getToggleState() ? getAccentOrange().darker(0.3f) : getMetalDark());
        g.fillRoundedRectangle(trackBounds, cornerSize);
        
        // Border
        g.setColour(button.getToggleState() ? getAccentOrange() : getMetalGrey());
        g.drawRoundedRectangle(trackBounds, cornerSize, 1.0f);
        
        // Thumb (circular)
        float thumbDiameter = bounds.getHeight() - 6.0f;
        float thumbX = button.getToggleState() 
            ? bounds.getRight() - thumbDiameter - 3.0f
            : bounds.getX() + 3.0f;
        float thumbY = bounds.getCentreY() - thumbDiameter * 0.5f;
        
        g.setColour(getMetalLight());
        g.fillEllipse(thumbX, thumbY, thumbDiameter, thumbDiameter);
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
    
    // v1.0.1: Image-based fader rendering with proper aspect ratio
    // fader.png is 1024x1536 - left half = empty track, right half = filled track
    if (faderImage.isValid()) {
        int imgWidth = faderImage.getWidth();
        int imgHeight = faderImage.getHeight();
        int halfWidth = imgWidth / 2;
        
        // v1.0.1: Calculate aspect ratio of one half (halfWidth x imgHeight)
        float srcAspect = static_cast<float>(halfWidth) / static_cast<float>(imgHeight);
        float boundsAspect = static_cast<float>(width) / static_cast<float>(height);
        
        // v1.0.1: Calculate destination size maintaining aspect ratio
        float destW, destH;
        if (boundsAspect > srcAspect) {
            // Bounds are wider - fit by height
            destH = static_cast<float>(height);
            destW = destH * srcAspect;
        } else {
            // Bounds are taller - fit by width
            destW = static_cast<float>(width);
            destH = destW / srcAspect;
        }
        
        // v1.0.1: Center in bounds
        float destX = static_cast<float>(x) + (static_cast<float>(width) - destW) * 0.5f;
        float destY = static_cast<float>(y) + (static_cast<float>(height) - destH) * 0.5f;
        
        // Calculate normalized position (inverted for vertical - 0 at bottom, 1 at top)
        // sliderPos is in pixel coordinates within the component
        float normalizedPos = 1.0f - (sliderPos - static_cast<float>(y)) / static_cast<float>(height);
        normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        
        // v1.0.1: High-quality scaling
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        
        // Draw the empty track (left half of image) - full track background
        g.drawImage(faderImage, 
                    destX, destY, destW, destH,
                    0, 0, halfWidth, imgHeight);
        
        // Calculate fill amount from bottom (in destination coordinates)
        float fillHeightDest = destH * normalizedPos;
        
        if (fillHeightDest > 1.0f) {
            // Calculate corresponding source coordinates
            int fillHeightSrc = static_cast<int>(static_cast<float>(imgHeight) * normalizedPos);
            if (fillHeightSrc > 0) {
                int srcY = imgHeight - fillHeightSrc;
                float destFillY = destY + destH - fillHeightDest;
                
                // Draw filled portion from right half, starting from bottom
                g.drawImage(faderImage,
                            destX, destFillY, destW, fillHeightDest,
                            halfWidth, srcY, halfWidth, fillHeightSrc);
            }
        }
    } else {
        // Fallback: simple vertical slider with thumb
        const float trackWidth = 24.0f;
        const float trackX = static_cast<float>(x) + (static_cast<float>(width) - trackWidth) * 0.5f;
        const float trackHeight = static_cast<float>(height);
        const float trackY = static_cast<float>(y);
        
        // Track background with gradient
        juce::ColourGradient trackGrad(getMetalDark().brighter(0.1f), trackX, trackY,
                                        getMetalDark().darker(0.2f), trackX + trackWidth, trackY + trackHeight, false);
        g.setGradientFill(trackGrad);
        g.fillRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 6.0f);
        
        // Inner shadow
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.drawRoundedRectangle(trackX + 1, trackY + 1, trackWidth - 2, trackHeight - 2, 5.0f, 1.0f);
        
        // Calculate fill
        float normalizedPos = 1.0f - (sliderPos - static_cast<float>(y)) / static_cast<float>(height);
        normalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        float fillHeight = (trackHeight - 8.0f) * normalizedPos;
        
        // Orange fill from bottom with gradient
        if (fillHeight > 2.0f) {
            float fillY = trackY + trackHeight - 4.0f - fillHeight;
            juce::ColourGradient fillGrad(getAccentOrangeBright(), trackX + 4, fillY,
                                           getAccentOrange().darker(0.2f), trackX + 4, fillY + fillHeight, false);
            g.setGradientFill(fillGrad);
            g.fillRoundedRectangle(trackX + 4.0f, fillY, trackWidth - 8.0f, fillHeight, 3.0f);
        }
        
        // Thumb
        const float thumbHeight = 22.0f;
        const float thumbWidth = trackWidth + 12.0f;
        const float thumbX = trackX - 6.0f;
        const float thumbY = juce::jlimit(static_cast<float>(y), 
                                          static_cast<float>(y + height) - thumbHeight, 
                                          sliderPos - thumbHeight * 0.5f);
        
        // Thumb with gradient
        juce::ColourGradient thumbGrad(getMetalLight(), thumbX, thumbY,
                                        getMetalGrey(), thumbX, thumbY + thumbHeight, false);
        g.setGradientFill(thumbGrad);
        g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);
        
        g.setColour(getMetalDark());
        g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f, 1.0f);
        
        // Thumb grip lines
        g.setColour(getMetalDark().withAlpha(0.5f));
        float lineY = thumbY + thumbHeight * 0.5f;
        g.drawLine(thumbX + 8.0f, lineY - 3.0f, thumbX + thumbWidth - 8.0f, lineY - 3.0f, 1.0f);
        g.drawLine(thumbX + 8.0f, lineY, thumbX + thumbWidth - 8.0f, lineY, 1.0f);
        g.drawLine(thumbX + 8.0f, lineY + 3.0f, thumbX + thumbWidth - 8.0f, lineY + 3.0f, 1.0f);
    }
}
