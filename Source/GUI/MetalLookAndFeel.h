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

    // Colors
    static juce::Colour getBackgroundDark()    { return juce::Colour(0xff1a1a1a); }
    static juce::Colour getAccentOrange()      { return juce::Colour(0xffcc6633); }
    static juce::Colour getAccentRed()         { return juce::Colour(0xffcc3333); }
    static juce::Colour getMetalGrey()         { return juce::Colour(0xff4a4a4a); }
    static juce::Colour getMetalLight()        { return juce::Colour(0xff666666); }
    static juce::Colour getTextLight()         { return juce::Colour(0xffe0e0e0); }
    static juce::Colour getTextDim()           { return juce::Colour(0xff888888); }
    static juce::Colour getLEDGreen()          { return juce::Colour(0xff33cc33); }
    static juce::Colour getLEDRed()            { return juce::Colour(0xffcc3333); }
    static juce::Colour getLEDOrange()         { return juce::Colour(0xffff9933); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetalLookAndFeel)
};
