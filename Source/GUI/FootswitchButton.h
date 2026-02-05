#pragma once
#include <JuceHeader.h>

class FootswitchButton : public juce::Component,
                         public juce::Timer {
public:
    enum LEDState { Off, Green, Red, OrangeBlinking };

    FootswitchButton();
    ~FootswitchButton() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void timerCallback() override;

    void setLEDState(LEDState state);
    LEDState getLEDState() const { return mLEDState; }
    
    void setOn(bool on);
    bool isOn() const { return mIsOn; }

    std::function<void(bool)> onClick;

private:
    bool mIsOn = true;
    bool mIsPressed = false;
    LEDState mLEDState = Green;
    bool mBlinkState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FootswitchButton)
};
