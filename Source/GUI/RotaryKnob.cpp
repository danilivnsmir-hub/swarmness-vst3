#include "RotaryKnob.h"
#include "MetalLookAndFeel.h"

RotaryKnob::RotaryKnob(const juce::String& labelText) {
    mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mSlider.setPopupDisplayEnabled(false, false, this);
    mSlider.addListener(this);
    addAndMakeVisible(mSlider);

    mLabel.setText(labelText, juce::dontSendNotification);
    mLabel.setJustificationType(juce::Justification::centred);
    mLabel.setFont(juce::Font(9.0f));
    mLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextLight());
    mLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(mLabel);

    // Value display label
    mValueLabel.setJustificationType(juce::Justification::centred);
    mValueLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    mValueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentRedBright());
    addAndMakeVisible(mValueLabel);
}

RotaryKnob::~RotaryKnob() {
    mSlider.removeListener(this);
}

void RotaryKnob::sliderValueChanged(juce::Slider* slider) {
    if (slider == &mSlider) {
        updateValueDisplay();
    }
}

void RotaryKnob::updateValueDisplay() {
    float value = static_cast<float>(mSlider.getValue()) * mValueMultiplier;
    juce::String valueText;
    
    if (mValueMultiplier == 100.0f) {
        // Percentage display
        valueText = juce::String(static_cast<int>(value)) + mValueSuffix;
    } else if (std::abs(value) < 10.0f) {
        // Small values with decimal
        valueText = juce::String(value, 2) + mValueSuffix;
    } else {
        // Larger values without decimal
        valueText = juce::String(static_cast<int>(value)) + mValueSuffix;
    }
    
    mValueLabel.setText(valueText, juce::dontSendNotification);
}

void RotaryKnob::resized() {
    auto bounds = getLocalBounds();
    const int labelHeight = 14;
    const int valueHeight = 12;
    const int ledSpace = 2;
    
    // Value at the very bottom
    auto valueBounds = bounds.removeFromBottom(valueHeight);
    mValueLabel.setBounds(valueBounds);
    
    // Label above value
    auto labelBounds = bounds.removeFromBottom(labelHeight);
    labelBounds = labelBounds.expanded(8, 0);
    mLabel.setBounds(labelBounds);
    
    // Slider takes the rest
    bounds.removeFromBottom(ledSpace);
    mSlider.setBounds(bounds.reduced(2));
    
    // Initial value update
    updateValueDisplay();
}

void RotaryKnob::paint(juce::Graphics& g) {
    // Draw small red LED dot indicator below the knob
    auto bounds = getLocalBounds();
    const int labelHeight = 14;
    const int valueHeight = 12;
    
    const float ledSize = 4.0f;
    const float ledX = bounds.getCentreX() - ledSize * 0.5f;
    const float ledY = bounds.getHeight() - labelHeight - valueHeight - ledSize - 2.0f;
    
    // LED glow
    g.setColour(MetalLookAndFeel::getAccentRedBright().withAlpha(0.3f));
    g.fillEllipse(ledX - 1, ledY - 1, ledSize + 2, ledSize + 2);
    
    // LED body
    g.setColour(MetalLookAndFeel::getAccentRedBright());
    g.fillEllipse(ledX, ledY, ledSize, ledSize);
}

void RotaryKnob::setLabelText(const juce::String& text) {
    mLabel.setText(text, juce::dontSendNotification);
}
