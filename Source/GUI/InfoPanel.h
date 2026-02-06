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
        
        // Red border
        g.setColour(MetalLookAndFeel::getAccentRed());
        g.drawRoundedRectangle(panelBounds.toFloat(), 8.0f, 2.0f);
        
        // Title
        g.setColour(MetalLookAndFeel::getAccentRed());
        g.setFont(juce::Font(20.0f, juce::Font::bold));
        g.drawText("SWARMNESS - Parameter Guide", panelBounds.getX() + 20, panelBounds.getY() + 15, 
                   panelBounds.getWidth() - 40, 30, juce::Justification::centred);
        
        // Parameter descriptions
        g.setColour(MetalLookAndFeel::getTextLight());
        g.setFont(juce::Font(11.0f));
        
        int y = panelBounds.getY() + 55;
        int lineHeight = 18;
        int col1X = panelBounds.getX() + 25;
        int col2X = panelBounds.getCentreX() + 10;
        int colWidth = (panelBounds.getWidth() - 60) / 2;
        
        // Left column
        drawParamInfo(g, col1X, y, colWidth, "VOLTAGE Section:", true);
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "GRAIN - Granular fragment variation");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "PITCH - Pitch instability amount");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "DRIFT - Pitch rise/fall time");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "OCT SELECT - Octave shift direction");
        y += lineHeight * 1.5f;
        
        drawParamInfo(g, col1X, y, colWidth, "PULSE Section:", true);
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "SLIDE TIME - Pitch glide duration");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "SLIDE RANGE - Pitch glide range");
        y += lineHeight * 1.5f;
        
        drawParamInfo(g, col1X, y, colWidth, "HIVE FILTER Section:", true);
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "CUTOFF - High-pass filter frequency");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "RESONANCE - Low-pass filter frequency");
        y += lineHeight;
        drawParamInfo(g, col1X, y, colWidth, "MID BOOST - Saturation/warmth");
        
        // Right column
        y = panelBounds.getY() + 55;
        drawParamInfo(g, col2X, y, colWidth, "SWARM Section:", true);
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "DEPTH - Swarm modulation intensity");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "RATE - Swarm modulation speed");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "MIX - Dry chorus/modulated blend");
        y += lineHeight * 1.5f;
        
        drawParamInfo(g, col2X, y, colWidth, "FLOW Section:", true);
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "FLOW AMOUNT - Gate/stutter depth");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "FLOW SPEED - Stutter rate (Hz)");
        y += lineHeight * 1.5f;
        
        drawParamInfo(g, col2X, y, colWidth, "OUTPUT Section:", true);
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "MIX - Wet/dry balance");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "DRIVE - Output saturation");
        y += lineHeight;
        drawParamInfo(g, col2X, y, colWidth, "OUTPUT LEVEL - Master volume");
        y += lineHeight * 1.5f;
        
        drawParamInfo(g, col2X, y, colWidth, "BYPASS - Enable/disable effect");
        
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
            g.setColour(MetalLookAndFeel::getAccentRed());
            g.setFont(juce::Font(12.0f, juce::Font::bold));
        } else {
            g.setColour(MetalLookAndFeel::getTextLight());
            g.setFont(juce::Font(10.5f));
        }
        g.drawText(text, x, y, width, 16, juce::Justification::centredLeft);
    }
};
