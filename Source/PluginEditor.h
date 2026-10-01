#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/SwarmLookAndFeel.h"
#include "GUI/Controls.h"
#include "GUI/ChainStrip.h"
#include "GUI/Pages.h"
#include "GUI/TunerOverlay.h"

/** All controls laid out at a fixed base resolution; the editor scales it as a whole. */
class MainPanel : public juce::Component
{
public:
    static constexpr int baseWidth  = 1100;
    static constexpr int baseHeight = 800;   // full view
    static constexpr int miniHeight = 330;   // MINI: chain, scenes and footswitches for playing live

    int getBaseHeight() const noexcept { return mini ? miniHeight : baseHeight; }
    bool isMini() const noexcept { return mini; }
    void setMini (bool shouldBeMini);
    std::function<void()> onModeChanged;   // the editor resizes

    explicit MainPanel (SwarmnessAudioProcessor&);

    void resized() override;
    void tick();   // called by the editor's timer

    enum Page { fxPageIndex = 0, eqPageIndex, spacePageIndex, pitchPageIndex, rigPageIndex, numPages };
    void showPage (int page);
    static int pageForBlock (int block);

private:
    /** Static artwork, cached as an image so animated controls repaint cheaply. */
    struct Backdrop : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void paintBackdrop (juce::Graphics&);

    using APVTS = juce::AudioProcessorValueTreeState;

    void attachButton (juce::Component& parent, juce::Button&, const juce::String& id, const juce::String& tooltip);
    bool paramOn (const char* id) const;
    bool footswitchesMomentary() const;
    void setSectionDimmed (std::initializer_list<juce::Component*>, bool dimmed);

    // MIDI learn: right-click any control (or a chain tile) for its menu
    void mouseDown (const juce::MouseEvent&) override;
    void showMidiMenu (const juce::String& paramID, juce::Component* target, int value = -1);
    juce::Component* findLearnable (const juce::String& paramID);
    struct LearnMarker : juce::Component
    {
        float phase = 0.0f;
        void paint (juce::Graphics&) override;
    };
    LearnMarker learnMarker;

    SwarmnessAudioProcessor& processor;
    APVTS& state;

    juce::Image logo, emblem;
    Backdrop backdrop;

    // Chain + pages (the FX page holds the original sections; EQ and CRYPT have their own components)
    ChainStrip chainStrip;
    juce::Component fxPage, pitchPage, rigPage;   // FX = SMOKE / SWARM / WINGS, PITCH = HIVE + SHIFT, RIG = WASP + AMP + CAB
    EqPage eqPage;
    ReverbPage reverbPage;
    WaspSection wasp;       // RIG page: the overdrive on top,
    AmpCabSection ampCab;   // AMP + CAB under it
    int currentPage = fxPageIndex;

    // Header
    PresetBar presetBar;
    SegmentedChoice switchModeSelector;
    juce::TextButton infoButton { "?" }, miniButton { "MINI" }, tunerButton { "TUNE" };
    SceneBar sceneBar;
    bool mini = false;

    // SHIFT (footswitch shifter)
    Knob shiftAKnob { "SHIFT A", true }, shiftBKnob { "SHIFT B", true }, riseKnob { "RISE" }, fallKnob { "FALL" }, blendKnob { "MIX" };
    PowerButton shiftPower;
    Knob panicKnob { "ANGER" }, chaosKnob { "FRENZY" }, speedKnob { "BUZZ" }, shDetuneKnob { "DETUNE", true };
    PillToggle stackToggle { "STACK" }, shSnapToggle { "SNAP" }, shRawToggle { "RAW" };
    PitchScope pitchScope;

    // HIVE: VOICES | TRAILS | MANGLE
    PowerButton hivePower;
    PillToggle snapToggle { "SNAP" };
    Knob pitchKnob { "PITCH", true }, primaryKnob { "DRONE" }, secondaryKnob { "QUEEN" }, trackingKnob { "TRACKING" };
    PillToggle rbSyncToggle { "SYNC" }, trDryToggle { "DRY" };
    Knob magicKnob { "TRAILS" }, rbTimeKnob { "TIME" }, rbDivKnob { "DIV" }, toneKnob { "TONE" }, gateKnob { "GATE" };
    StepGrid stepGrid;
    PillToggle rbRawToggle { "RAW" };
    Knob hvMangleKnob { "MANGLE" }, rbDetuneKnob { "DETUNE", true }, rbMixKnob { "MIX" };

    // SWARM
    PowerButton swarmPower;
    PillToggle deepToggle { "DEEP" };
    Knob swarmDepthKnob { "DEPTH" }, swarmRateKnob { "RATE" }, swarmMixKnob { "MIX" };

    // SMOKE (fuzz)
    PowerButton fuzzPower;
    SegmentedChoice fuzzVoiceSelector;
    Knob fuzzKnob { "FUZZ" }, fuzzToneKnob { "TONE" }, fuzzScoopKnob { "SCOOP" };
    Knob fuzzGlareKnob { "GLARE" }, fuzzGateKnob { "GATE" }, fuzzSagKnob { "SAG" }, fuzzBlendKnob { "CLEAN" };

    // HONEY (compressor)
    PowerButton honeyPower;
    PillToggle limitToggle { "LIMIT" };
    Knob honeySustainKnob { "SUSTAIN" }, honeyAttackKnob { "ATTACK" }, honeyBlendKnob { "BLEND" }, honeyLevelKnob { "LEVEL", true };
    GainReductionMeter honeyMeter;

    // WINGS (gate)
    PowerButton flowPower;
    PillToggle hardToggle { "HARD" }, syncToggle { "SYNC" };
    Knob flowAmountKnob { "AMOUNT" }, flowSpeedKnob { "SPEED" }, flowDivKnob { "DIV" };
    StepGrid wingsGrid;

    // Levels (footer, next to the meters)
    Knob inputKnob { "INPUT", true }, volumeKnob { "VOLUME", true };

    // Footswitches
    Footswitch oct1Switch, oct2Switch, magicSwitch, bypassSwitch;
    MiniSwitch link1Switch { "LINK" }, link2Switch { "LINK" };
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };

    InfoOverlay infoOverlay;
    TunerOverlay tunerOverlay;

    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;

    // Section rectangles (base coordinates)
    juce::Rectangle<float> hiveArea, shiftArea, swarmArea, fuzzArea, flowArea, honeyArea, footswitchArea;
    std::array<juce::Rectangle<float>, 3> hiveSections;   // VOICES, TRAILS, MANGLE
    std::array<bool, 6> lastSectionStates {};   // SHIFT engaged, HIVE on, SWARM, SMOKE, WINGS, HONEY
    int lastShiftA = 999, lastShiftB = 999;

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
    void applyMode (float scale);

    SwarmnessAudioProcessor& swarmProcessor;
    SwarmLookAndFeel lookAndFeel;
    MainPanel panel;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmnessAudioProcessorEditor)
};
