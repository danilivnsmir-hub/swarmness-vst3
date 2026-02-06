#pragma once
#include <JuceHeader.h>

class MetalLookAndFeel : public juce::LookAndFeel_V4 {
public:
    MetalLookAndFeel();
    ~MetalLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g, int width, int height,
                      bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    // v3.0.0: Yellow-Orange Bee color scheme
    static juce::Colour getBackgroundDark()    { return juce::Colour(0xff0a0a0a); }  // Almost black
    static juce::Colour getPanelBackground()   { return juce::Colour(0xff151515); }  // Slightly lighter
    static juce::Colour getAccentOrange()      { return juce::Colour(0xffFFB300); }  // Main accent - amber
    static juce::Colour getAccentOrangeBright(){ return juce::Colour(0xffFFC107); }  // Brighter for arcs/LEDs
    static juce::Colour getAccentOrangeDim()   { return juce::Colour(0xff996600); }  // Dim for inactive
    static juce::Colour getAccentYellow()      { return juce::Colour(0xffFFD54F); }  // Yellow highlight
    static juce::Colour getMetalLight()        { return juce::Colour(0xffc0c0c0); }  // Silver highlight
    static juce::Colour getMetalMid()          { return juce::Colour(0xff909090); }  // Mid silver
    static juce::Colour getMetalGrey()         { return juce::Colour(0xff606060); }  // Dark metal
    static juce::Colour getMetalDark()         { return juce::Colour(0xff404040); }  // Very dark metal
    static juce::Colour getTextLight()         { return juce::Colour(0xffe8e8e8); }  // White text
    static juce::Colour getTextDim()           { return juce::Colour(0xff888888); }  // Dimmed text
    static juce::Colour getLEDGreen()          { return juce::Colour(0xff33cc55); }  // Green LED
    static juce::Colour getLEDOrange()         { return juce::Colour(0xffFF8F00); }  // Orange LED
    static juce::Colour getLEDYellow()         { return juce::Colour(0xffFFD700); }  // Yellow LED
    
    // Legacy aliases for compatibility (now map to orange)
    static juce::Colour getAccentRed()         { return getAccentOrange(); }
    static juce::Colour getAccentRedBright()   { return getAccentOrangeBright(); }
    static juce::Colour getAccentRedDim()      { return getAccentOrangeDim(); }
    static juce::Colour getLEDRed()            { return getLEDOrange(); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetalLookAndFeel)
};
