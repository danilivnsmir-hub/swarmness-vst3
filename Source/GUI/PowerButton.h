#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

// Power button that uses the 2x2 grid from power_button.png
// Top-left: gray OFF, Top-right: orange ON
// Bottom-left: gray OFF alt, Bottom-right: orange ON alt
// Phase 3 UI: Includes glow pulse animation on enable
class PowerButton : public juce::Button, public juce::Timer {
public:
    PowerButton() : juce::Button("PowerButton") {
        // Enable toggle behavior - base class handles state changes
        setClickingTogglesState(true);
        
        // Load the power button image
        powerImage = juce::ImageCache::getFromMemory(
            BinaryData::power_button_png, BinaryData::power_button_pngSize);
    }
    
    ~PowerButton() override {
        stopTimer();
    }
    
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, 
                     bool shouldDrawButtonAsDown) override {
        auto bounds = getLocalBounds().toFloat();
        
        // Phase 3 UI: Draw animated glow pulse when ON
        if (getToggleState() && mGlowAlpha > 0.0f) {
            float glowRadius = bounds.getWidth() * 0.6f;
            juce::ColourGradient glowGrad(
                juce::Colour(0xffFF8C00).withAlpha(mGlowAlpha * 0.6f),
                bounds.getCentreX(), bounds.getCentreY(),
                juce::Colour(0xffFF8C00).withAlpha(0.0f),
                bounds.getCentreX() + glowRadius, bounds.getCentreY(), true);
            g.setGradientFill(glowGrad);
            g.fillEllipse(bounds.getCentreX() - glowRadius, bounds.getCentreY() - glowRadius,
                         glowRadius * 2.0f, glowRadius * 2.0f);
        }
        
        if (powerImage.isValid()) {
            // Image is 2x2 grid - use top row (simpler look)
            // OFF = top-left quadrant, ON = top-right quadrant
            int imgW = powerImage.getWidth() / 2;
            int imgH = powerImage.getHeight() / 2;
            
            int srcX = getToggleState() ? imgW : 0;  // ON = right column
            int srcY = 0;  // Top row
            
            // Use bottom row for hover/pressed states
            if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown) {
                srcY = imgH;
            }
            
            g.drawImage(powerImage,
                bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                srcX, srcY, imgW, imgH);
        } else {
            // Fallback drawing if image not loaded
            auto center = bounds.getCentre();
            float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.35f;
            
            juce::Colour color = getToggleState() 
                ? juce::Colour(0xffFF8C00)  // Orange when ON
                : juce::Colour(0xff606060); // Gray when OFF
            
            // Draw power symbol
            g.setColour(color);
            
            // Circle arc
            juce::Path arc;
            arc.addCentredArc(center.x, center.y, radius, radius, 
                              0.0f, juce::MathConstants<float>::pi * 0.3f, 
                              juce::MathConstants<float>::pi * 1.7f, true);
            g.strokePath(arc, juce::PathStrokeType(2.5f));
            
            // Vertical line
            g.drawLine(center.x, center.y - radius * 0.9f,
                       center.x, center.y + radius * 0.1f, 2.5f);
            
            // Glow effect when ON
            if (getToggleState()) {
                g.setColour(color.withAlpha(0.4f));
                g.fillEllipse(center.x - radius * 1.3f, center.y - radius * 1.3f,
                              radius * 2.6f, radius * 2.6f);
            }
        }
    }
    
    // Phase 3 UI: Trigger glow pulse when state changes to ON
    void buttonStateChanged() override {
        if (getToggleState() && !mWasOn) {
            // Just turned ON - start glow pulse animation
            mGlowAlpha = 1.0f;
            startTimerHz(60);
        }
        mWasOn = getToggleState();
    }
    
    // Phase 3 UI: Animate glow fade out
    void timerCallback() override {
        mGlowAlpha -= 0.05f;  // Fade out over ~300ms
        if (mGlowAlpha <= 0.0f) {
            mGlowAlpha = 0.0f;
            stopTimer();
        }
        repaint();
    }
    
    // NOTE: Do NOT override clicked() - it causes infinite recursion when
    // used with ButtonParameterAttachment. setClickingTogglesState(true) in
    // constructor handles toggle behavior properly.
    
private:
    juce::Image powerImage;
    
    // Phase 3 UI: Glow animation state
    float mGlowAlpha = 0.0f;
    bool mWasOn = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PowerButton)
};
