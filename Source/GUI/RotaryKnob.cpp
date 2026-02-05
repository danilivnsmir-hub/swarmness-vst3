#include "RotaryKnob.h"
#include "MetalLookAndFeel.h"

RotaryKnob::RotaryKnob(const juce::String& labelText) {
    mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mSlider.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(mSlider);

    mLabel.setText(labelText, juce::dontSendNotification);
    mLabel.setJustificationType(juce::Justification::centred);
    mLabel.setFont(juce::Font(11.0f));
    mLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextLight());
    addAndMakeVisible(mLabel);
}

void RotaryKnob::resized() {
    auto bounds = getLocalBounds();
    auto labelHeight = 16;
    
    mLabel.setBounds(bounds.removeFromBottom(labelHeight));
    mSlider.setBounds(bounds.reduced(4));
}

void RotaryKnob::paint(juce::Graphics& g) {
    // Optional background
}

void RotaryKnob::setLabelText(const juce::String& text) {
    mLabel.setText(text, juce::dontSendNotification);
}
