#include "RotaryKnob.h"
#include "MetalLookAndFeel.h"

RotaryKnob::RotaryKnob(const juce::String& labelText) {
    mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mSlider.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(mSlider);

    mLabel.setText(labelText, juce::dontSendNotification);
    mLabel.setJustificationType(juce::Justification::centred);
    mLabel.setFont(juce::Font(10.0f));
    mLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextLight());
    addAndMakeVisible(mLabel);
}

void RotaryKnob::resized() {
    auto bounds = getLocalBounds();
    const int labelHeight = 14;
    const int ledSpace = 4;
    
    // Label at bottom
    mLabel.setBounds(bounds.removeFromBottom(labelHeight));
    
    // Slider takes the rest (minus LED space)
    bounds.removeFromBottom(ledSpace);
    mSlider.setBounds(bounds.reduced(2));
}

void RotaryKnob::paint(juce::Graphics& g) {
    // Draw small red LED dot indicator below the knob
    auto bounds = getLocalBounds();
    const int labelHeight = 14;
    
    const float ledSize = 5.0f;
    const float ledX = bounds.getCentreX() - ledSize * 0.5f;
    const float ledY = bounds.getHeight() - labelHeight - ledSize - 2.0f;
    
    // LED glow
    g.setColour(MetalLookAndFeel::getAccentRedBright().withAlpha(0.3f));
    g.fillEllipse(ledX - 2, ledY - 2, ledSize + 4, ledSize + 4);
    
    // LED body
    g.setColour(MetalLookAndFeel::getAccentRedBright());
    g.fillEllipse(ledX, ledY, ledSize, ledSize);
}

void RotaryKnob::setLabelText(const juce::String& text) {
    mLabel.setText(text, juce::dontSendNotification);
}
