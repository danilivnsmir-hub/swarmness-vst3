#pragma once
#include <JuceHeader.h>

class RotaryKnob : public juce::Component, public juce::Slider::Listener {
public:
    RotaryKnob(const juce::String& labelText = "");
    ~RotaryKnob() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void sliderValueChanged(juce::Slider* slider) override;

    juce::Slider& getSlider() { return mSlider; }
    void setLabelText(const juce::String& text);
    void setValueSuffix(const juce::String& suffix) { mValueSuffix = suffix; }
    void setValueMultiplier(float mult) { mValueMultiplier = mult; }

private:
    juce::Slider mSlider;
    juce::Label mLabel;
    juce::Label mValueLabel;
    juce::String mValueSuffix;
    float mValueMultiplier = 1.0f;

    void updateValueDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnob)
};
