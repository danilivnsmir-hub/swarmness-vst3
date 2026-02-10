#include "RotaryKnob.h"
#include "MetalLookAndFeel.h"

RotaryKnob::RotaryKnob(const juce::String& labelText) {
    mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mSlider.setPopupDisplayEnabled(false, false, this);
    mSlider.addListener(this);
    addAndMakeVisible(mSlider);
    
    // Phase 3 UI: Start timer for smooth value transitions
    startTimerHz(60);

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
    stopTimer();  // Phase 3 UI: Stop animation timer
    mSlider.removeListener(this);
    mValueLabel.removeListener(this);
}

void RotaryKnob::sliderValueChanged(juce::Slider* slider) {
    if (slider == &mSlider && !mIsEditingValue) {
        updateValueDisplay();
        // Phase 3 UI: Set target for smooth animation
        mTargetValue = static_cast<float>(mSlider.getValue());
    }
}

// Phase 3 UI: Smooth value transitions
void RotaryKnob::timerCallback() {
    // Smoothly interpolate display value towards target
    float diff = mTargetValue - mDisplayValue;
    if (std::abs(diff) > 0.0001f) {
        mDisplayValue += diff * kSmoothingCoeff;
        // Store smoothed value for LookAndFeel to use
        mSlider.getProperties().set("smoothedValue", mDisplayValue);
        mSlider.repaint();
    }
}

void RotaryKnob::labelTextChanged(juce::Label* labelThatHasChanged) {
    if (labelThatHasChanged == &mValueLabel && mIsEditingValue) {
        float newValue = parseValueFromText(mValueLabel.getText());
        
        // Convert back based on scale mode
        switch (mScaleMode) {
            case ScaleMode::Scale1to10_Step01:
            case ScaleMode::Scale1to10_Step05:
                // Convert from 1.0-10.0 back to 0-1 normalized
                newValue = (newValue - 1.0f) / 9.0f;
                break;
            case ScaleMode::Percent:
            case ScaleMode::Custom:
            default:
                // Convert back to slider value (divide by multiplier)
                if (mValueMultiplier != 0.0f) {
                    newValue /= mValueMultiplier;
                }
                break;
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
    
    float normalizedValue = static_cast<float>(mSlider.getValue());
    juce::String valueText;
    
    switch (mScaleMode) {
        case ScaleMode::Scale1to10_Step01: {
            // Convert 0-1 to 1.0-10.0 with 0.1 step precision
            float displayValue = 1.0f + normalizedValue * 9.0f;
            valueText = juce::String(displayValue, 1);  // 1 decimal place
            break;
        }
        case ScaleMode::Scale1to10_Step05: {
            // Convert 0-1 to 1.0-10.0 with 0.5 step precision (rounded)
            float displayValue = 1.0f + normalizedValue * 9.0f;
            float rounded = std::round(displayValue * 2.0f) / 2.0f;  // Round to nearest 0.5
            valueText = juce::String(rounded, 1);  // 1 decimal place
            break;
        }
        case ScaleMode::Percent: {
            // Traditional percentage display
            float value = normalizedValue * mValueMultiplier;
            valueText = juce::String(static_cast<int>(value)) + mValueSuffix;
            break;
        }
        case ScaleMode::Custom:
        default: {
            // Custom display (semitones, Hz, ms, etc.)
            float value = normalizedValue * mValueMultiplier;
            if (std::abs(value) < 10.0f && mValueMultiplier == 1.0f && !mValueSuffix.contains("st")) {
                valueText = juce::String(value, 1) + mValueSuffix;
            } else {
                valueText = juce::String(static_cast<int>(value)) + mValueSuffix;
            }
            break;
        }
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
    
    // Phase 3 UI: Initialize smoothed value
    mTargetValue = static_cast<float>(mSlider.getValue());
    mDisplayValue = mTargetValue;
}

void RotaryKnob::paint(juce::Graphics& g) {
    // Phase 1 UI improvement: Removed red LED indicators under knobs
    juce::ignoreUnused(g);
}

// Phase 3 UI: Hover effects
void RotaryKnob::mouseEnter(const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    mIsHovered = true;
    mSlider.getProperties().set("isHovered", true);
    mSlider.repaint();
}

void RotaryKnob::mouseExit(const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    mIsHovered = false;
    mSlider.getProperties().set("isHovered", false);
    mSlider.repaint();
}

void RotaryKnob::setLabelText(const juce::String& text) {
    mLabel.setText(text, juce::dontSendNotification);
}
