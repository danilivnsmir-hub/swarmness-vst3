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
    mLabel.setFont(juce::Font(11.0f));  // Phase 1 UI: Increased from 9px to 11px
    mLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getTextLight());
    mLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(mLabel);

    // Editable value display label
    mValueLabel.setJustificationType(juce::Justification::centred);
    mValueLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    mValueLabel.setColour(juce::Label::textColourId, MetalLookAndFeel::getAccentOrangeBright());
    mValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    mValueLabel.setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff2a2a2a));
    mValueLabel.setColour(juce::Label::outlineWhenEditingColourId, MetalLookAndFeel::getAccentOrange());
    mValueLabel.setEditable(false, true, false);  // singleClick=false, doubleClick=true
    mValueLabel.addListener(this);
    addAndMakeVisible(mValueLabel);
}

RotaryKnob::~RotaryKnob() {
    mSlider.removeListener(this);
    mValueLabel.removeListener(this);
}

void RotaryKnob::sliderValueChanged(juce::Slider* slider) {
    if (slider == &mSlider && !mIsEditingValue) {
        updateValueDisplay();
    }
}

void RotaryKnob::labelTextChanged(juce::Label* labelThatHasChanged) {
    if (labelThatHasChanged == &mValueLabel && mIsEditingValue) {
        float newValue = parseValueFromText(mValueLabel.getText());
        
        // Convert back to slider value (divide by multiplier)
        if (mValueMultiplier != 0.0f) {
            newValue /= mValueMultiplier;
        }
        
        // Clamp to slider range
        newValue = juce::jlimit(static_cast<float>(mSlider.getMinimum()), 
                                static_cast<float>(mSlider.getMaximum()), 
                                newValue);
        
        mSlider.setValue(static_cast<double>(newValue), juce::sendNotificationSync);
    }
}

void RotaryKnob::editorShown(juce::Label* label, juce::TextEditor& editor) {
    if (label == &mValueLabel) {
        mIsEditingValue = true;
        editor.setJustification(juce::Justification::centred);
        editor.setColour(juce::TextEditor::textColourId, MetalLookAndFeel::getAccentOrangeBright());
        editor.setColour(juce::TextEditor::highlightColourId, MetalLookAndFeel::getAccentOrange().withAlpha(0.4f));
        
        // Select all text when editing starts
        editor.selectAll();
    }
}

void RotaryKnob::editorHidden(juce::Label* label, juce::TextEditor& editor) {
    if (label == &mValueLabel) {
        mIsEditingValue = false;
        updateValueDisplay();  // Refresh display with proper formatting
    }
}

float RotaryKnob::parseValueFromText(const juce::String& text) {
    // Remove suffix and any whitespace
    juce::String cleanText = text.trim();
    
    // Remove known suffixes
    cleanText = cleanText.replace(mValueSuffix, "", true);
    cleanText = cleanText.replace("st", "", true);
    cleanText = cleanText.replace("ms", "", true);
    cleanText = cleanText.replace("Hz", "", true);
    cleanText = cleanText.replace("dB", "", true);
    cleanText = cleanText.replace("%", "", true);
    cleanText = cleanText.replace("k", "000", true);  // Handle kHz
    cleanText = cleanText.trim();
    
    return cleanText.getFloatValue();
}

void RotaryKnob::updateValueDisplay() {
    if (mIsEditingValue) return;
    
    float value = static_cast<float>(mSlider.getValue()) * mValueMultiplier;
    juce::String valueText;
    
    if (mValueMultiplier == 100.0f) {
        // Percentage display
        valueText = juce::String(static_cast<int>(value)) + mValueSuffix;
    } else if (std::abs(value) < 10.0f && mValueMultiplier == 1.0f) {
        // Small values with decimal (but not for semitones)
        if (mValueSuffix.contains("st")) {
            valueText = juce::String(static_cast<int>(value)) + mValueSuffix;
        } else {
            valueText = juce::String(value, 1) + mValueSuffix;
        }
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
    // Phase 1 UI improvement: Removed red LED indicators under knobs
    juce::ignoreUnused(g);
}

void RotaryKnob::setLabelText(const juce::String& text) {
    mLabel.setText(text, juce::dontSendNotification);
}
