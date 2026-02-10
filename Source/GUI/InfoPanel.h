#pragma once
#include <JuceHeader.h>
#include "MetalLookAndFeel.h"

class InfoPanel : public juce::Component {
public:
    InfoPanel() {
        setVisible(false);
    }
    
    void paint(juce::Graphics& g) override {
        // Dark overlay background
        g.fillAll(juce::Colour(0xee101010));
        
        // Panel background
        auto panelBounds = getLocalBounds().reduced(40, 60);
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(panelBounds.toFloat(), 8.0f);
        
        // Orange border
        g.setColour(MetalLookAndFeel::getAccentOrange());
        g.drawRoundedRectangle(panelBounds.toFloat(), 8.0f, 2.0f);
        
        // Title
        g.setColour(MetalLookAndFeel::getAccentOrange());
        g.setFont(juce::Font(20.0f, juce::Font::bold));
        g.drawText("SWARMNESS v1.2.0 - Parameter Guide", panelBounds.getX() + 20, panelBounds.getY() + 15, 
                   panelBounds.getWidth() - 40, 30, juce::Justification::centred);
        
        // Parameter descriptions
        g.setColour(MetalLookAndFeel::getTextLight());
        g.setFont(juce::Font(11.0f));
        
        int y = panelBounds.getY() + 55;
        int lineHeight = 17;
        int col1X = panelBounds.getX() + 25;
        int col2X = panelBounds.getCentreX() + 10;
        int colWidth = (panelBounds.getWidth() - 60) / 2;
        
        // Left column
        drawParamInfo(g, col1X, y, colWidth, "PITCH + MODULATION Section:", true);
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "ON/OFF - Enable/disable pitch shifter & modulation");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "OCTAVE - Pitch shift (-2, -1, 0, +1, +2 octaves)");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "RANGE - Random pitch variation (0-24 semitones)");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "SPEED - Random pitch change rate (0.1-10 Hz)");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "RISE - Pitch glide time (0-2000 ms)");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "ANGER - Chaos intensity, unpredictable pitch");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "RUSH - Glitch intensity, stuttering artifacts");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "RATE - Modulation speed");
        y += lineHeight * 1.2f;
        
        drawParamInfo(g, col1X, y, colWidth, "TONE Section:", true);
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "LOW CUT - High-pass filter (20-2000 Hz)");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "HIGH CUT - Low-pass filter (1k-20k Hz)");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "MID BOOST - Mid frequency saturation");
        
        // Right column
        y = panelBounds.getY() + 55;
        drawParamInfo(g, col2X, y, colWidth, "SWARM Section (Chorus):", true);
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "ON/OFF - Enable/disable chorus effect");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "DEPTH - Chorus depth/intensity");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "RATE - Chorus modulation rate (0.1-10 Hz)");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "MIX - Chorus wet/dry mix");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "DEEP - Classic/Deep chorus mode");
        y += lineHeight * 1.2f;
        
        drawParamInfo(g, col2X, y, colWidth, "FLOW Section (Stutter/Gate):", true);
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "ON/OFF - Enable/disable stutter/gate effect");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "AMOUNT - Stutter/gate intensity");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "SPEED - Stutter rate (1-32 Hz)");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "HARD - Smooth/Hard gate mode");
        y += lineHeight * 1.2f;
        
        drawParamInfo(g, col2X, y, colWidth, "OUTPUT Section:", true);
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "MIX - Overall wet/dry balance");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "VOLUME - Output level (-24 to +12 dB)");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "DRIVE - Soft clipping/saturation");
        
        // Close hint
        g.setColour(MetalLookAndFeel::getTextDim());
        g.setFont(juce::Font(10.0f));
        g.drawText("Click anywhere to close", panelBounds.getX(), panelBounds.getBottom() - 30, 
                   panelBounds.getWidth(), 20, juce::Justification::centred);
    }
    
    void mouseDown(const juce::MouseEvent& e) override {
        setVisible(false);
    }
    
private:
    void drawParamInfo(juce::Graphics& g, int x, int y, int width, const juce::String& text, bool isHeader = false) {
        if (isHeader) {
            g.setColour(MetalLookAndFeel::getAccentOrange());
            g.setFont(juce::Font(12.0f, juce::Font::bold));
        } else {
            g.setColour(MetalLookAndFeel::getTextLight());
            g.setFont(juce::Font(10.5f));
        }
        g.drawText(text, x, y, width, 16, juce::Justification::centredLeft);
    }
};
