#pragma once
#include <JuceHeader.h>

class RotaryKnob : public juce::Component, 
                   public juce::Slider::Listener,
                   public juce::Label::Listener,
                   public juce::Timer {
public:
    RotaryKnob(const juce::String& labelText = "");
    ~RotaryKnob() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void labelTextChanged(juce::Label* labelThatHasChanged) override;
    void editorShown(juce::Label* label, juce::TextEditor& editor) override;
    void editorHidden(juce::Label* label, juce::TextEditor& editor) override;
    void timerCallback() override;  // Phase 3 UI: Smooth value transitions
    
    // Phase 3 UI: Hover effects
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    enum class ScaleMode { Percent, Scale1to10_Step01, Scale1to10_Step05, Custom };
    
    juce::Slider& getSlider() { return mSlider; }
    void setLabelText(const juce::String& text);
    void setValueSuffix(const juce::String& suffix) { mValueSuffix = suffix; }
    void setValueMultiplier(float mult) { mValueMultiplier = mult; }
    void setScaleMode(ScaleMode mode) { mScaleMode = mode; }
    bool isHovered() const { return mIsHovered; }

private:
    juce::Slider mSlider;
    juce::Label mLabel;
    juce::Label mValueLabel;
    juce::String mValueSuffix;
    float mValueMultiplier = 1.0f;
    ScaleMode mScaleMode = ScaleMode::Percent;
    bool mIsEditingValue = false;
    bool mIsHovered = false;  // Phase 3 UI: Hover state
    
    // Phase 3 UI: Smooth value animation
    float mDisplayValue = 0.0f;
    float mTargetValue = 0.0f;
    static constexpr float kSmoothingCoeff = 0.15f;  // ~50ms transition at 60fps

    void updateValueDisplay();
    float parseValueFromText(const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnob)
};
