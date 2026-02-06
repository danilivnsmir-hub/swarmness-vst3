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
    setColour(juce::PopupMenu::highlightedBackgroundColourId, getAccentRed());
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
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

    // === RED ARC INDICATOR (behind knob) ===
    const float arcRadius = radius - 1.0f;
    const float arcThickness = 3.5f;
    
    // Background arc (dark)
    {
        juce::Path bgArc;
        bgArc.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(getAccentRedDim());
        g.strokePath(bgArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Value arc (bright red)
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                               0.0f, rotaryStartAngle, angle, true);
        g.setColour(getAccentRedBright());
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

    juce::Colour baseColour = shouldDrawButtonAsDown ? getAccentRed().darker(0.2f) :
                              shouldDrawButtonAsHighlighted ? getMetalGrey() : getMetalDark();

    if (button.getToggleState())
        baseColour = getAccentRed();

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
    
    // Red accent border
    g.setColour(getAccentRedDim());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    // Arrow (red)
    juce::Path arrow;
    float arrowSize = 5.0f;
    float arrowX = width - 12.0f;
    float arrowY = height * 0.5f;
    arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.4f,
                      arrowX + arrowSize, arrowY - arrowSize * 0.4f,
                      arrowX, arrowY + arrowSize * 0.6f);
    g.setColour(getAccentRedBright());
    g.fillPath(arrow);
}

void MetalLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(getPanelBackground());
    g.setColour(getAccentRedDim());
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
        g.setColour(getAccentRed());
        g.fillRect(r);
        g.setColour(juce::Colours::white);
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
    const float cornerSize = 3.0f;
    
    // Background
    juce::Colour bgColour = button.getToggleState() ? getAccentRed() : getMetalDark();
    if (shouldDrawButtonAsHighlighted)
        bgColour = bgColour.brighter(0.1f);
    if (shouldDrawButtonAsDown)
        bgColour = bgColour.darker(0.1f);
    
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.reduced(1), cornerSize);
    
    // Border
    g.setColour(button.getToggleState() ? getAccentRedBright() : getMetalGrey());
    g.drawRoundedRectangle(bounds.reduced(1), cornerSize, 1.0f);
    
    // Small LED indicator on left
    const float ledSize = 6.0f;
    const float ledX = bounds.getX() + 6.0f;
    const float ledY = bounds.getCentreY() - ledSize * 0.5f;
    
    g.setColour(button.getToggleState() ? getAccentRedBright() : getAccentRedDim());
    g.fillEllipse(ledX, ledY, ledSize, ledSize);
    
    // Text
    g.setColour(getTextLight());
    g.setFont(juce::Font(10.0f));
    auto textBounds = bounds.reduced(2).withTrimmedLeft(ledSize + 6);
    g.drawText(button.getButtonText(), textBounds, juce::Justification::centred, false);
}
