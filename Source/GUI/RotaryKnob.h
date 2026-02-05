#pragma once
#include <JuceHeader.h>

class RotaryKnob : public juce::Component {
public:
    RotaryKnob(const juce::String& labelText = "");
    ~RotaryKnob() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    juce::Slider& getSlider() { return mSlider; }
    void setLabelText(const juce::String& text);

private:
    juce::Slider mSlider;
    juce::Label mLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnob)
};
