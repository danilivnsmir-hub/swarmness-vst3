#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/SwarmLookAndFeel.h"
#include "GUI/Controls.h"

/** All controls laid out at a fixed base resolution; the editor scales it as a whole. */
class MainPanel : public juce::Component
{
public:
    static constexpr int baseWidth  = 1000;
    static constexpr int baseHeight = 640;

    explicit MainPanel (SwarmnessAudioProcessor&);

    void resized() override;
    void tick();   // called by the editor's timer

private:
    /** Static artwork, cached as an image so animated controls repaint cheaply. */
    struct Backdrop : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void paintBackdrop (juce::Graphics&);

    using APVTS = juce::AudioProcessorValueTreeState;

    void attachButton (juce::Button&, const juce::String& id, const juce::String& tooltip);
    bool paramOn (const char* id) const;
    void setSectionDimmed (std::initializer_list<juce::Component*>, bool dimmed);

    SwarmnessAudioProcessor& processor;
    APVTS& state;

    juce::Image logo;
    Backdrop backdrop;

    // Header
    PresetBar presetBar;
    juce::TextButton infoButton { "?" };

    // VOLTAGE
    PowerButton pitchPower;
    SegmentedChoice octaveSelector;
    SegmentedChoice qualitySelector;
    Knob semiKnob { "SEMI", true }, riseKnob { "RISE" }, rangeKnob { "RANGE" }, speedKnob { "SPEED" };
    Knob rushKnob { "RUSH" }, angerKnob { "ANGER" }, modRateKnob { "RATE" };
    PitchScope pitchScope;

    // TONE
    Fader lowCutFader { "LOW CUT" }, highCutFader { "HIGH CUT" }, midFader { "MID" };

    // OUTPUT
    Fader mixFader { "MIX" }, driveFader { "DRIVE" }, volumeFader { "VOLUME", true };

    // SWARM
    PowerButton swarmPower;
    PillToggle deepToggle { "DEEP" };
    Knob swarmDepthKnob { "DEPTH" }, swarmRateKnob { "RATE" }, swarmMixKnob { "MIX" };

    // FLOW
    PowerButton flowPower;
    PillToggle hardToggle { "HARD" }, syncToggle { "SYNC" };
    Knob flowAmountKnob { "AMOUNT" }, flowSpeedKnob { "SPEED" }, flowDivKnob { "DIV" };

    // Footer
    Footswitch footswitch;
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };

    InfoOverlay infoOverlay;

    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;

    // Section rectangles (base coordinates)
    juce::Rectangle<float> voltageArea, toneArea, outputArea, swarmArea, flowArea;
    juce::String latencyText;
    std::array<bool, 3> lastSectionStates { true, true, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

//==============================================================================
class SwarmnessAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SwarmnessAudioProcessorEditor (SwarmnessAudioProcessor&);
    ~SwarmnessAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls meter / preset / state updates from the processor (normally driven by the timer). */
    void refresh() { panel.tick(); }

private:
    void timerCallback() override { refresh(); }

    SwarmnessAudioProcessor& swarmProcessor;
    SwarmLookAndFeel lookAndFeel;
    MainPanel panel;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessorEditor)
};
