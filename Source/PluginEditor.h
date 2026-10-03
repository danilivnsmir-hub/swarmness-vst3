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
    static constexpr int baseHeight = 660;   // full view: the chain, ONE block's page, scenes, footswitches
    static constexpr int miniHeight = 362;   // MINI: chain, scenes and footswitches for playing live

    int getBaseHeight() const noexcept { return mini ? miniHeight : baseHeight; }
    bool isMini() const noexcept { return mini; }
    void setMini (bool shouldBeMini);
    std::function<void()> onModeChanged;   // the editor resizes

    explicit MainPanel (SwarmnessAudioProcessor&);

    ~MainPanel() override;

    void resized() override;
    void tick();   // called by the editor's timer

    /** One page per block, in the default chain order; AMP and CAB share the RIG page (they are tuned together).
        A click on a chain tile opens its page. */
    enum Page { honeyPage = 0, smokePage, shiftPage, hivePage, waspPage, rigPage, swarmPage, wingsPage, combPage, carvePage, cryptPage, numPages };
    void showPage (int page);
    static int pageForBlock (int block);
    static bool pageShowsBlock (int page, int block) { return pageForBlock (block) == page; }

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
    void showMidiMenu (const juce::String& paramID, juce::Component* target, int value = -1, int block = -1);
    void addStompWiring (juce::PopupMenu&, bool venom);
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

    // Chain + pages: the blocks drawn by the panel itself live in a holder per page (full-panel coordinates);
    // WASP, AMP + CAB, COMB / CARVE and CRYPT are components of their own
    ChainStrip chainStrip;
    std::array<juce::Component, numPages> pages;
    EqPage eqPage;          // COMB or CARVE, one at a time
    ReverbPage reverbPage;
    WaspSection wasp;
    AmpCabSection ampCab;
    int currentPage = rigPage;
    juce::Rectangle<int> pageArea;

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
    PillToggle rbSyncToggle { "SYNC" }, trDryToggle { "DRY" }, hvStopToggle { "STOP" };
    Knob magicKnob { "TRAILS" }, rbTimeKnob { "TIME" }, rbDivKnob { "DIV" }, toneKnob { "TONE" }, gateKnob { "GATE" }, hvStopTimeKnob { "FALL" }, hvStopRiseKnob { "RISE" };
    StepGrid stepGrid;
    PillToggle rbRawToggle { "RAW" };
    Knob hvMangleKnob { "MANGLE" }, rbDetuneKnob { "DETUNE", true }, rbMixKnob { "MIX" };

    // SWARM
    PowerButton swarmPower;
    PillToggle deepToggle { "DEEP" };
    Knob swarmDepthKnob { "DEPTH" }, swarmRateKnob { "RATE" }, swarmMixKnob { "MIX" }, swarmRingKnob { "RING" };

    // SMOKE (fuzz)
    PowerButton fuzzPower;
    SegmentedChoice fuzzVoiceSelector;
    Knob fuzzKnob { "FUZZ" }, fuzzToneKnob { "TONE" }, fuzzScoopKnob { "SCOOP" };
    Knob fuzzGlareKnob { "GLARE" }, fuzzGateKnob { "GATE" }, fuzzSagKnob { "SAG" }, fuzzBlendKnob { "CLEAN" }, fuzzCrushKnob { "CRUSH" };

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
    Knob inputKnob { "INPUT", true }, volumeKnob { "VOLUME", true }, inGateKnob { "GATE" };

    // Footswitches
    Footswitch oct1Switch, oct2Switch, magicSwitch, stingSwitch, bypassSwitch;
    bool blockEngaged (int block) const;
    MiniSwitch link1Switch { "LINK" }, link2Switch { "LINK" };
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    juce::TextButton learnButton { "LEARN" };
    PillToggle outFreezeToggle { "FREEZE" }, outStopToggle { "STOP" };

    InfoOverlay infoOverlay;

    /** The licence: ACTIVATE / DEACTIVATE with a key, the trial countdown, the store link. */
    struct LicencePanel : public juce::Component
    {
        explicit LicencePanel (SwarmnessAudioProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override { if (! card.contains (e.position)) setVisible (false); }
        void refresh();
        SwarmnessAudioProcessor& processor;
        juce::Rectangle<float> card;
        juce::TextEditor keyEditor;
        juce::TextButton activateButton { "ACTIVATE" }, deactivateButton { "DEACTIVATE" }, buyButton { "BUY A LICENCE" }, closeButton { "CLOSE" };
        juce::String status;
    };
    LicencePanel licencePanel;
    juce::TextButton licenceButton;   // bottom-left: TRIAL - n DAYS / TRIAL OVER - ACTIVATE
    void refreshLicence();
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
