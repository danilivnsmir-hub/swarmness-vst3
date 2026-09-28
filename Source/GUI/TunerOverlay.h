#pragma once

#include <JuceHeader.h>
#include "Controls.h"
#include "../DSP/Tuner.h"

/**
 * TUNER - the necro-bee way: a big note in the display font, a row of honeycomb cells from
 * -50 to +50 cents that light up where the string is, the centre cells glowing honey when it is in
 * tune. MUTE silences the output while it is open; A4 reference 430..450 Hz. Opens over the whole
 * panel; click outside the panel (or CLOSE) to leave.
 */
class TunerOverlay : public juce::Component,
                     private juce::Timer
{
public:
    explicit TunerOverlay (TunerTap&);
    ~TunerOverlay() override;

    void open();
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Note name, octave and cents for a frequency (tests). */
    struct Reading { int midi = -1; float cents = 0.0f; };
    static Reading read (float hz, float a4);
    static juce::String noteName (int midi);
    void timerCallbackForTests() { timerCallback(); }

private:
    void timerCallback() override;

    TunerTap& tap;
    juce::Rectangle<float> panelArea, meterArea;
    PillToggle muteToggle { "MUTE" };
    juce::TextButton closeButton { "CLOSE" }, refDown { "-" }, refUp { "+" };
    std::vector<float> window;
    float a4 = 440.0f;
    float hz = 0.0f, shownCents = 0.0f;
    int midi = -1, silentTicks = 999;
    float glow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerOverlay)
};
