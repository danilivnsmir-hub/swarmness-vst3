#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/SwarmLookAndFeel.h"
#include "GUI/Controls.h"

/** All controls laid out at a fixed base resolution; the editor scales it as a whole. */
class MainPanel : public juce::Component
{
public:
    static constexpr int baseWidth  = 1100;
    static constexpr int baseHeight = 680;

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
    bool footswitchesMomentary() const;
    void setSectionDimmed (std::initializer_list<juce::Component*>, bool dimmed);

    SwarmnessAudioProcessor& processor;
    APVTS& state;

    juce::Image logo;
    Backdrop backdrop;

    // Header
    PresetBar presetBar;
    SegmentedChoice switchModeSelector;
    juce::TextButton infoButton { "?" };

    // STING (internally "noise")
    PillToggle downToggle { "DIVE" };
    Knob riseKnob { "RISE" }, fallKnob { "FALL" }, panicKnob { "ANGER" }, chaosKnob { "FRENZY" }, speedKnob { "BUZZ" };
    Knob stingMixKnob { "MIX" };
    PitchScope pitchScope;

    // HIVE (internally "rainbow")
    PowerButton rainbowPower;
    PillToggle snapToggle { "SNAP" };
    Knob pitchKnob { "PITCH", true }, primaryKnob { "DRONE" }, secondaryKnob { "QUEEN" };
    Knob toneKnob { "TONE" }, trackingKnob { "TRACKING" }, magicKnob { "VENOM" };

    // SWARM
    PowerButton swarmPower;
    PillToggle deepToggle { "DEEP" };
    Knob swarmDepthKnob { "DEPTH" }, swarmRateKnob { "RATE" }, swarmMixKnob { "MIX" };

    // SMOKE (fuzz)
    PowerButton fuzzPower;
    PillToggle postToggle { "POST" };
    SegmentedChoice fuzzVoiceSelector;
    Knob fuzzKnob { "FUZZ" }, fuzzToneKnob { "TONE" }, fuzzScoopKnob { "SCOOP" };
    Knob fuzzGlareKnob { "GLARE" }, fuzzGateKnob { "GATE" }, fuzzBlendKnob { "BLEND" };

    // WINGS (gate)
    PowerButton flowPower;
    PillToggle hardToggle { "HARD" }, syncToggle { "SYNC" };
    Knob flowAmountKnob { "AMOUNT" }, flowSpeedKnob { "SPEED" }, flowDivKnob { "DIV" };

    // OUTPUT
    Knob volumeKnob { "VOLUME", true };

    // Footswitches
    Footswitch oct1Switch, oct2Switch, magicSwitch, bypassSwitch;
    MiniSwitch link1Switch { "LINK" }, link2Switch { "LINK" };
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };

    InfoOverlay infoOverlay;

    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;

    // Section rectangles (base coordinates)
    juce::Rectangle<float> noiseArea, rainbowArea, swarmArea, fuzzArea, flowArea, outputArea;
    std::array<bool, 5> lastSectionStates {};

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
