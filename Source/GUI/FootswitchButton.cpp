#include "FootswitchButton.h"
#include "MetalLookAndFeel.h"

FootswitchButton::FootswitchButton() {
    startTimerHz(4); // For blinking LED
}

void FootswitchButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float centreX = w * 0.5f;
    const float centreY = h * 0.5f;
    
    // === HEXAGONAL OUTER BEZEL ===
    const float hexRadius = juce::jmin(w, h) * 0.45f;
    {
        juce::Path hexPath;
        for (int i = 0; i < 6; i++) {
            float angle = juce::MathConstants<float>::pi / 6.0f + i * juce::MathConstants<float>::pi / 3.0f;
            float hx = centreX + hexRadius * std::cos(angle);
            float hy = centreY + hexRadius * std::sin(angle);
            if (i == 0) hexPath.startNewSubPath(hx, hy);
            else hexPath.lineTo(hx, hy);
        }
        hexPath.closeSubPath();
        
        // Hexagon gradient (brushed metal) - darker when pressed
        juce::Colour topCol = mIsPressed ? MetalLookAndFeel::getMetalGrey() : MetalLookAndFeel::getMetalLight();
        juce::Colour botCol = mIsPressed ? MetalLookAndFeel::getMetalDark() : MetalLookAndFeel::getMetalGrey();
        
        juce::ColourGradient hexGrad(topCol, centreX, centreY - hexRadius,
                                      botCol, centreX, centreY + hexRadius, false);
        g.setGradientFill(hexGrad);
        g.fillPath(hexPath);
        
        // Hex border - thicker dark edge
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.strokePath(hexPath, juce::PathStrokeType(2.5f));
        
        // Inner highlight
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.strokePath(hexPath, juce::PathStrokeType(1.0f));
    }

    // === OUTER RING (grooved rim) ===
    const float outerRingRadius = hexRadius - 8.0f;
    {
        juce::ColourGradient ringGrad(MetalLookAndFeel::getMetalLight().brighter(0.1f), 
                                       centreX, centreY - outerRingRadius,
                                       MetalLookAndFeel::getMetalDark(), 
                                       centreX, centreY + outerRingRadius, false);
        g.setGradientFill(ringGrad);
        g.fillEllipse(centreX - outerRingRadius, centreY - outerRingRadius, 
                      outerRingRadius * 2.0f, outerRingRadius * 2.0f);
        
        // Groove shadow
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawEllipse(centreX - outerRingRadius, centreY - outerRingRadius,
                      outerRingRadius * 2.0f, outerRingRadius * 2.0f, 2.0f);
    }

    // === INNER GROOVE ===
    const float innerGrooveRadius = hexRadius - 14.0f;
    {
        g.setColour(MetalLookAndFeel::getMetalDark().darker(0.3f));
        g.fillEllipse(centreX - innerGrooveRadius, centreY - innerGrooveRadius,
                      innerGrooveRadius * 2.0f, innerGrooveRadius * 2.0f);
    }

    // === CENTER CAP (main button surface) ===
    const float capRadius = hexRadius - 18.0f;
    const float pressOffset = mIsPressed ? 2.0f : 0.0f;
    {
        // Button cap gradient
        juce::Colour capTop = mIsPressed ? MetalLookAndFeel::getMetalGrey() 
                                          : MetalLookAndFeel::getMetalLight().brighter(0.2f);
        juce::Colour capBot = mIsPressed ? MetalLookAndFeel::getMetalDark() 
                                          : MetalLookAndFeel::getMetalMid();
        
        juce::ColourGradient capGrad(capTop, centreX, centreY - capRadius + pressOffset,
                                      capBot, centreX, centreY + capRadius + pressOffset, false);
        g.setGradientFill(capGrad);
        g.fillEllipse(centreX - capRadius, centreY - capRadius + pressOffset,
                      capRadius * 2.0f, capRadius * 2.0f);
        
        // Highlight arc
        if (!mIsPressed) {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            juce::Path highlightArc;
            highlightArc.addCentredArc(centreX, centreY, capRadius - 3, capRadius - 3,
                                       0.0f, -2.8f, -0.3f, true);
            g.strokePath(highlightArc, juce::PathStrokeType(3.0f));
        }
        
        // Border
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawEllipse(centreX - capRadius, centreY - capRadius + pressOffset,
                      capRadius * 2.0f, capRadius * 2.0f, 1.5f);
    }

    // === LED INDICATOR (top, above button) ===
    const float ledSize = 10.0f;
    const float ledX = centreX - ledSize * 0.5f;
    const float ledY = centreY - hexRadius - ledSize - 8.0f;
    
    if (ledY > 0) {
        // Determine LED color
        juce::Colour ledColour;
        bool isLit = true;
        
        switch (mLEDState) {
            case Off:
                ledColour = MetalLookAndFeel::getAccentRedDim().darker(0.5f);
                isLit = false;
                break;
            case Green:
                ledColour = MetalLookAndFeel::getLEDGreen();
                break;
            case Red:
            case BrightRed:
                ledColour = MetalLookAndFeel::getLEDRed();
                break;
            case OrangeBlinking:
                isLit = mBlinkState;
                ledColour = mBlinkState ? MetalLookAndFeel::getLEDOrange() 
                                        : MetalLookAndFeel::getAccentRedDim().darker(0.5f);
                break;
            case DimRed:
                ledColour = MetalLookAndFeel::getAccentRedDim();
                isLit = false;
                break;
        }

        // LED glow
        if (isLit) {
            juce::ColourGradient glowGrad(ledColour.withAlpha(0.5f), ledX + ledSize * 0.5f, ledY + ledSize * 0.5f,
                                           ledColour.withAlpha(0.0f), ledX + ledSize * 0.5f, ledY - ledSize, true);
            g.setGradientFill(glowGrad);
            g.fillEllipse(ledX - ledSize, ledY - ledSize, ledSize * 3.0f, ledSize * 3.0f);
        }

        // LED body
        g.setColour(ledColour);
        g.fillEllipse(ledX, ledY, ledSize, ledSize);
        
        // LED bezel
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawEllipse(ledX, ledY, ledSize, ledSize, 1.0f);
        
        // LED highlight
        if (isLit) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.fillEllipse(ledX + 2.0f, ledY + 2.0f, 3.0f, 3.0f);
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
