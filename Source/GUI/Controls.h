#pragma once

#include "Theme.h"
#include "../Preset/PresetManager.h"
#include <array>
#include <vector>

//==============================================================================
/** Rotary knob with caption above and live value readout below. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption, bool bipolar = false);

    juce::Slider& getSlider() noexcept { return slider; }
    void attach (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& tooltip);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String caption;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
/** Vertical studio-style fader with caption and value readout. */
class Fader : public juce::Component
{
public:
    Fader (const juce::String& caption, bool bipolar = false);

    juce::Slider& getSlider() noexcept { return slider; }
    void attach (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& tooltip);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String caption;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fader)
};

//==============================================================================
/** Round section power switch with glowing ring. */
class PowerButton : public juce::ToggleButton
{
public:
    PowerButton();
    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
};

//==============================================================================
/** Small labelled pill toggle with an LED dot (e.g. DEEP, HARD, SYNC). */
class PillToggle : public juce::ToggleButton
{
public:
    explicit PillToggle (const juce::String& label);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
};

//==============================================================================
/** Segmented selector bound to a choice parameter (octave, quality...). */
class SegmentedChoice : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    SegmentedChoice (juce::RangedAudioParameter& param, juce::StringArray labels);
    /** Free-standing variant (no parameter): onSelect is called when the user clicks a segment. */
    explicit SegmentedChoice (juce::StringArray labels);

    void setSelectedIndex (int index);
    int getSelectedIndex() const noexcept { return selected; }
    std::function<void (int)> onSelect;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<float>) const;

    juce::StringArray labels;
    int selected = 0, hovered = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedChoice)
};

//==============================================================================
/**
 * Metal stomp switch bound to a boolean parameter: LED on top, caption below.
 * Momentary switches are "on" only while held (mouse down .. mouse up); latching ones toggle.
 */
class Footswitch : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    Footswitch (juce::RangedAudioParameter& param, const juce::String& caption, juce::Colour ledColour,
                bool ledShowsInverse, std::function<bool()> isMomentary);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Lights the LED although the parameter is off (e.g. engaged through a LINK switch). */
    void setLitExternally (bool shouldBeLit);

private:
    juce::String caption;
    juce::Colour ledColour;
    bool inverse = false, value = false, pressed = false, holding = false, externallyLit = false;
    std::function<bool()> momentary;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footswitch)
};

//==============================================================================
/** Miniature toggle switch (bat lever) with a tiny caption, e.g. the LINK switches. */
class MiniSwitch : public juce::ToggleButton
{
public:
    explicit MiniSwitch (const juce::String& caption);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
};

//==============================================================================
/** Stereo horizontal peak meter with hold. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& caption);

    /** Linear peak levels since the last update. */
    void update (float left, float right);
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
    std::array<float, 2> level {}, hold {};
    std::array<int, 2> holdTicks {};
};

//==============================================================================
/** Scrolling display of the live transposition in semitones. */
class PitchScope : public juce::Component
{
public:
    PitchScope();

    void push (float semitones, bool active);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> history;
    int writeIndex = 0;
    bool active = true, primed = false;
    float current = 0.0f;
};

//==============================================================================
/**
 * Header preset browser: [FACTORY | USER] < [name] > SAVE ...
 * The bank tabs choose which presets the list and the arrows browse; the bank follows
 * whatever preset gets loaded (e.g. saving switches to USER).
 */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager&);

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showActionsMenu();
    void saveAs();
    void confirmDelete();
    void importPreset();
    void exportPreset();

    void setBank (bool user);
    void showMenuForBank();

    PresetManager& presets;
    SegmentedChoice bankTabs { { "FACTORY", "USER" } };
    bool userBank = false;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, nameButton, saveButton { "SAVE" }, menuButton { "..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownName;
    bool shownDirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

//==============================================================================
/** Help overlay describing every control. Click anywhere to close. */
class InfoOverlay : public juce::Component
{
public:
    InfoOverlay();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
