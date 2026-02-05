#include "MetalLookAndFeel.h"

MetalLookAndFeel::MetalLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, getBackgroundDark());
    setColour(juce::Label::textColourId, getTextLight());
    setColour(juce::Slider::textBoxTextColourId, getTextLight());
    setColour(juce::Slider::textBoxOutlineColourId, getMetalGrey());
    setColour(juce::ComboBox::backgroundColourId, getMetalGrey());
    setColour(juce::ComboBox::textColourId, getTextLight());
    setColour(juce::ComboBox::arrowColourId, getTextLight());
    setColour(juce::ComboBox::outlineColourId, getMetalLight());
    setColour(juce::PopupMenu::backgroundColourId, getMetalGrey());
    setColour(juce::PopupMenu::textColourId, getTextLight());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, getAccentOrange());
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, getMetalGrey());
    setColour(juce::TextButton::textColourOffId, getTextLight());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void MetalLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider) {
    const float radius = juce::jmin(width / 2.0f, height / 2.0f) - 4.0f;
    const float centreX = x + width * 0.5f;
    const float centreY = y + height * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Background circle with gradient
    juce::ColourGradient bgGradient(getMetalLight(), centreX, centreY - radius,
                                     getMetalGrey(), centreX, centreY + radius, false);
    g.setGradientFill(bgGradient);
    g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

    // Outer ring
    g.setColour(juce::Colours::black);
    g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 2.0f);

    // Value arc
    juce::Path arcPath;
    arcPath.addCentredArc(centreX, centreY, radius - 3.0f, radius - 3.0f,
                          0.0f, rotaryStartAngle, angle, true);
    g.setColour(getAccentOrange());
    g.strokePath(arcPath, juce::PathStrokeType(3.0f));

    // Pointer line
    juce::Path pointerPath;
    const float pointerLength = radius * 0.6f;
    const float pointerThickness = 3.0f;
    pointerPath.addRectangle(-pointerThickness * 0.5f, -radius + 6.0f, pointerThickness, pointerLength);
    g.setColour(getTextLight());
    g.fillPath(pointerPath, juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    // Center cap
    g.setColour(getMetalGrey().darker(0.3f));
    g.fillEllipse(centreX - 8.0f, centreY - 8.0f, 16.0f, 16.0f);
}

void MetalLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    juce::Colour baseColour = shouldDrawButtonAsDown ? getAccentOrange().darker(0.2f) :
                              shouldDrawButtonAsHighlighted ? getMetalLight() : getMetalGrey();

    if (button.getToggleState())
        baseColour = getAccentOrange();

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void MetalLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
                                     bool isButtonDown, int buttonX, int buttonY,
                                     int buttonW, int buttonH, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(getMetalGrey());
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(getMetalLight());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Arrow
    juce::Path arrow;
    float arrowSize = 6.0f;
    float arrowX = width - 15.0f;
    float arrowY = height * 0.5f;
    arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.5f,
                      arrowX + arrowSize, arrowY - arrowSize * 0.5f,
                      arrowX, arrowY + arrowSize * 0.5f);
    g.setColour(getTextLight());
    g.fillPath(arrow);
}

void MetalLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(getMetalGrey());
    g.setColour(getMetalLight());
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
        g.setColour(getTextDim());
        g.fillRect(r.removeFromTop(1.0f));
        return;
    }

    auto r = area.reduced(1);

    if (isHighlighted && isActive) {
        g.setColour(getAccentOrange());
        g.fillRect(r);
        g.setColour(juce::Colours::white);
    } else {
        g.setColour(isActive ? getTextLight() : getTextDim());
    }

    auto font = juce::Font(14.0f);
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
