#include "FootswitchButton.h"
#include "MetalLookAndFeel.h"

FootswitchButton::FootswitchButton() {
    startTimerHz(4); // For blinking LED
}

void FootswitchButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    
    // Metal background with gradient
    juce::ColourGradient bgGradient(MetalLookAndFeel::getMetalLight(), 0, 0,
                                     MetalLookAndFeel::getMetalGrey(), 0, bounds.getHeight(),
                                     false);
    g.setGradientFill(bgGradient);
    g.fillRoundedRectangle(bounds.reduced(2), 8.0f);

    // Border
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(bounds.reduced(2), 8.0f, 2.0f);

    // Pressed effect
    if (mIsPressed) {
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.reduced(2), 8.0f);
    }

    // LED indicator
    float ledSize = 16.0f;
    float ledX = bounds.getCentreX() - ledSize * 0.5f;
    float ledY = 12.0f;

    // LED glow
    juce::Colour ledColour;
    switch (mLEDState) {
        case Off:
            ledColour = MetalLookAndFeel::getMetalGrey().darker();
            break;
        case Green:
            ledColour = MetalLookAndFeel::getLEDGreen();
            break;
        case Red:
            ledColour = MetalLookAndFeel::getLEDRed();
            break;
        case OrangeBlinking:
            ledColour = mBlinkState ? MetalLookAndFeel::getLEDOrange() 
                                    : MetalLookAndFeel::getMetalGrey().darker();
            break;
        case DimRed:
            ledColour = MetalLookAndFeel::getLEDRed().withAlpha(0.3f);
            break;
        case BrightRed:
            ledColour = MetalLookAndFeel::getLEDRed();
            break;
    }

    if (mLEDState != Off) {
        g.setColour(ledColour.withAlpha(0.3f));
        g.fillEllipse(ledX - 4, ledY - 4, ledSize + 8, ledSize + 8);
    }

    g.setColour(ledColour);
    g.fillEllipse(ledX, ledY, ledSize, ledSize);

    g.setColour(juce::Colours::black);
    g.drawEllipse(ledX, ledY, ledSize, ledSize, 1.0f);

    // Highlight on LED
    if (mLEDState != Off) {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillEllipse(ledX + 3, ledY + 3, 4, 4);
    }

    // Button label
    g.setColour(MetalLookAndFeel::getTextLight());
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(mIsOn ? "ON" : "OFF", bounds.reduced(4).withTrimmedTop(ledSize + 16),
               juce::Justification::centred, false);
}

void FootswitchButton::resized() {
    // Component doesn't need special resizing
}

void FootswitchButton::mouseDown(const juce::MouseEvent& event) {
    mIsPressed = true;
    repaint();
}

void FootswitchButton::mouseUp(const juce::MouseEvent& event) {
    mIsPressed = false;
    
    if (getLocalBounds().contains(event.getPosition())) {
        setOn(!mIsOn);
        if (onClick)
            onClick(mIsOn);
    }
    repaint();
}

void FootswitchButton::timerCallback() {
    if (mLEDState == OrangeBlinking) {
        mBlinkState = !mBlinkState;
        repaint();
    }
}

void FootswitchButton::setLEDState(LEDState state) {
    mLEDState = state;
    repaint();
}

void FootswitchButton::setOn(bool on) {
    mIsOn = on;
    if (mLEDState != OrangeBlinking) {
        mLEDState = on ? Green : Red;
    }
    repaint();
}
