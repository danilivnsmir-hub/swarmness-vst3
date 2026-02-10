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
    
    // v1.0.0: Image-based footswitch rendering
    if (footswitchImage.isValid()) {
        // Draw the base footswitch image
        float pressOffset = mIsPressed ? 2.0f : 0.0f;
        
        // Apply slight darkening when pressed
        if (mIsPressed) {
            g.setColour(juce::Colours::black.withAlpha(0.1f));
        }
        
        g.drawImage(footswitchImage, 
                    bounds.getX(), bounds.getY() + pressOffset, 
                    bounds.getWidth(), bounds.getHeight() - pressOffset,
                    0, 0, footswitchImage.getWidth(), footswitchImage.getHeight());
        
        // Draw LED glow on top when ON
        if (mIsOn) {
            const float ledSize = w * 0.15f;
            const float ledX = centreX - ledSize * 0.5f;
            const float ledY = h * 0.08f;  // Top of button
            
            // Orange LED glow
            juce::Colour ledColour = MetalLookAndFeel::getLEDOrange();
            
            // Handle blinking state
            if (mLEDState == OrangeBlinking && !mBlinkState) {
                ledColour = ledColour.withAlpha(0.3f);
            }
            
            // Outer glow
            juce::ColourGradient glowGrad(ledColour.withAlpha(0.6f), centreX, ledY + ledSize * 0.5f,
                                           ledColour.withAlpha(0.0f), centreX, ledY - ledSize * 2.0f, true);
            g.setGradientFill(glowGrad);
            g.fillEllipse(ledX - ledSize, ledY - ledSize, ledSize * 3.0f, ledSize * 3.0f);
            
            // LED center
            g.setColour(ledColour);
            g.fillEllipse(ledX, ledY, ledSize, ledSize);
            
            // LED highlight
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.fillEllipse(ledX + 2.0f, ledY + 2.0f, ledSize * 0.3f, ledSize * 0.3f);
        }
    } else {
        // Fallback: simple circle if image not loaded
        const float radius = juce::jmin(w, h) * 0.4f;
        
        g.setColour(mIsPressed ? MetalLookAndFeel::getMetalGrey() : MetalLookAndFeel::getMetalLight());
        g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        
        g.setColour(MetalLookAndFeel::getMetalDark());
        g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 2.0f);
        
        // LED indicator
        if (mIsOn) {
            g.setColour(MetalLookAndFeel::getLEDOrange());
            g.fillEllipse(centreX - 6, 8, 12, 12);
        }
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
