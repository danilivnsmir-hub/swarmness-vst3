#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

// Power button that uses the 2x2 grid from power_button.png
// Top-left: gray OFF, Top-right: orange ON
// Bottom-left: gray OFF alt, Bottom-right: orange ON alt
class PowerButton : public juce::Button {
public:
    PowerButton() : juce::Button("PowerButton") {
        // Load the power button image
        powerImage = juce::ImageCache::getFromMemory(
            BinaryData::power_button_png, BinaryData::power_button_pngSize);
    }
    
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, 
                     bool shouldDrawButtonAsDown) override {
        auto bounds = getLocalBounds().toFloat();
        
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
    
    void clicked() override {
        setToggleState(!getToggleState(), juce::sendNotification);
    }
    
private:
    juce::Image powerImage;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PowerButton)
};
