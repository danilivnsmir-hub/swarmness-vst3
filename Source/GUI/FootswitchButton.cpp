#include "FootswitchButton.h"
#include "MetalLookAndFeel.h"

FootswitchButton::FootswitchButton() {
    startTimerHz(4); // For blinking LED
    
    // v1.0.0: Load footswitch image
    footswitchImage = juce::ImageCache::getFromMemory(BinaryData::footswitch_png, BinaryData::footswitch_pngSize);
}

void FootswitchButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float centreX = w * 0.5f;
    const float centreY = h * 0.5f;
    
    // v1.2.1: Minimalist programmatic footswitch
    const float radius = juce::jmin(w, h) * 0.35f;
    const float pressOffset = mIsPressed ? 2.0f : 0.0f;
    
    // Outer ring (orange when ON, dark when OFF)
    juce::Colour ringColour = mIsOn ? MetalLookAndFeel::getAccentOrange() : juce::Colour(0xff333333);
    g.setColour(ringColour);
    g.fillEllipse(centreX - radius - 4, centreY - radius - 4 + pressOffset, (radius + 4) * 2.0f, (radius + 4) * 2.0f);
    
    // Inner button body (dark)
    g.setColour(juce::Colour(0xff1A1A1A));
    g.fillEllipse(centreX - radius, centreY - radius + pressOffset, radius * 2.0f, radius * 2.0f);
    
    // Button highlight (subtle gradient)
    juce::ColourGradient highlight(juce::Colour(0x25FFFFFF), centreX, centreY - radius + pressOffset,
                                    juce::Colours::transparentBlack, centreX, centreY + pressOffset, false);
    g.setGradientFill(highlight);
    g.fillEllipse(centreX - radius + 4, centreY - radius + 4 + pressOffset, (radius - 4) * 2.0f, (radius - 4) * 2.0f);
    
    // LED indicator (top center)
    const float ledSize = 10.0f;
    const float ledY = centreY - radius + 14 + pressOffset;
    
    // Handle blinking
    bool showLED = mIsOn;
    if (mLEDState == OrangeBlinking && !mBlinkState) {
        showLED = false;
    }
    
    if (showLED) {
        // LED glow
        juce::ColourGradient glowGrad(MetalLookAndFeel::getAccentOrange().withAlpha(0.5f), centreX, ledY + ledSize * 0.5f,
                                       juce::Colours::transparentBlack, centreX, ledY - ledSize, true);
        g.setGradientFill(glowGrad);
        g.fillEllipse(centreX - ledSize * 1.5f, ledY - ledSize * 0.5f, ledSize * 3.0f, ledSize * 2.0f);
        
        // LED body
        g.setColour(MetalLookAndFeel::getAccentOrange());
        g.fillEllipse(centreX - ledSize * 0.5f, ledY, ledSize, ledSize);
        
        // LED highlight
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillEllipse(centreX - ledSize * 0.25f, ledY + 2, ledSize * 0.4f, ledSize * 0.4f);
    } else {
        // Dim LED when OFF
        g.setColour(juce::Colour(0xff333333));
        g.fillEllipse(centreX - ledSize * 0.5f, ledY, ledSize, ledSize);
    }
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
        mLEDState = on ? BrightRed : DimRed;
    }
    repaint();
}
