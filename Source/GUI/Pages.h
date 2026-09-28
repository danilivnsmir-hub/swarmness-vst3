#pragma once

#include "Controls.h"
#include "../PluginProcessor.h"

//==============================================================================
/**
 * Frequency response display (20 Hz .. 20 kHz, log). Optionally with a live spectrum
 * analyser behind the curve and draggable band nodes bound to parameters.
 */
class EqGraph : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    struct Node
    {
        juce::String label;
        const char* freqId = nullptr;
        const char* gainId = nullptr;   // nullptr: the node only moves sideways (cuts)
        const char* qId = nullptr;      // mouse wheel
    };

    EqGraph (juce::AudioProcessorValueTreeState&, float dbRange);

    std::function<float (double freq)> response;                 // whole curve (dB)
    std::function<float (int node, double freq)> nodeResponse;   // one band (dB), optional
    std::vector<Node> nodes;

    void setAnalyser (SpectrumTap* tap, std::function<double()> sampleRate);
    void setSelectedNode (int index);
    int getSelectedNode() const noexcept { return selected; }
    std::function<void (int)> onNodeSelected;

    /** From the editor timer: analyser frames and parameter changes. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override { invalidateGrid(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float xForFreq (double f) const noexcept;
    double freqForX (float x) const noexcept;
    float yForDb (float db) const noexcept;
    float dbForY (float y) const noexcept;

private:
    juce::Point<float> nodePosition (int index) const;
    int nodeAt (juce::Point<float>) const;
    float paramValue (const char* id) const;
    void setParamValue (const char* id, float value);
    void invalidateGrid() { grid = {}; }
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (1.0f); }

    juce::AudioProcessorValueTreeState& state;
    float dbRange;
    int selected = -1, hovered = -1, dragging = -1;
    juce::Image grid;

    // analyser
    SpectrumTap* tap = nullptr;
    std::function<double()> getSampleRate;
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ring = std::vector<float> ((size_t) fftSize, 0.0f), fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> spectrum = std::vector<float> ((size_t) fftSize / 2, -120.0f), pullBuffer = std::vector<float> (8192, 0.0f);
    int ringPos = 0;
    bool haveSpectrum = false;

    std::vector<float> lastParamValues;
};

//==============================================================================
/** COMB (graphic EQ) and CARVE (parametric EQ). */
class EqPage : public juce::Component
{
public:
    explicit EqPage (SwarmnessAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    void selectBand (int band);   // 0 = LOW shelf, 1..3 = bells, 4 = HIGH shelf

    SwarmnessAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    juce::Rectangle<float> combArea, carveArea;
    bool combOn = false, carveOn = false;

    PowerButton combPower, carvePower;
    EqGraph combGraph, carveGraph;
    std::vector<std::unique_ptr<Fader>> combFaders;

    SegmentedChoice bandSelector { { "LOW", "1", "2", "3", "HIGH" } };
    Knob lowCutKnob { "LOW CUT" }, highCutKnob { "HIGH CUT" };
    Knob lowFreqKnob { "FREQ" }, lowGainKnob { "GAIN", true }, highFreqKnob { "FREQ" }, highGainKnob { "GAIN", true };
    std::array<Knob, 3> bellFreqKnobs { Knob { "FREQ" }, Knob { "FREQ" }, Knob { "FREQ" } };
    std::array<Knob, 3> bellGainKnobs { Knob { "GAIN", true }, Knob { "GAIN", true }, Knob { "GAIN", true } };
    std::array<Knob, 3> bellQKnobs { Knob { "Q" }, Knob { "Q" }, Knob { "Q" } };
    int selectedBand = 2;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqPage)
};

//==============================================================================
/** Picture of the reverb: pre-delay gap, build-up and decay (or the loaded IR), glowing with the live tail. */
class TailView : public juce::Component
{
public:
    explicit TailView (SwarmnessAudioProcessor&);
    void tick (float wetLevel);
    void paint (juce::Graphics&) override;

private:
    SwarmnessAudioProcessor& processor;
    std::vector<float> history = std::vector<float> (160, 0.0f);
    int historyPos = 0;
    float glow = 0.0f;
};

/** CRYPT reverb page. */
class ReverbPage : public juce::Component,
                   public juce::FileDragAndDropTarget
{
public:
    explicit ReverbPage (SwarmnessAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void load (const juce::File&);
    void chooseFile();

    SwarmnessAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    juce::Rectangle<float> panelArea, irArea;
    bool on = false, irMode = false, dragHover = false;
    juce::String irDescription, message;
    int messageTicks = 0;

    PowerButton power;
    SegmentedChoice typeSelector;
    TailView tail;
    juce::TextButton loadButton { "LOAD IR" }, clearButton { "CLEAR" };
    Knob mixKnob { "MIX" }, decayKnob { "DECAY" }, sizeKnob { "SIZE" }, preDelayKnob { "PRE-DELAY" };
    Knob toneKnob { "TONE" }, lowCutKnob { "LOW CUT" }, modKnob { "MOD" }, duckKnob { "DUCK" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPage)
};

//==============================================================================
/** AMP + CAB, on the RIG page under WASP. */
class AmpCabSection : public juce::Component,
                      public juce::FileDragAndDropTarget
{
public:
    explicit AmpCabSection (SwarmnessAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override { dragTarget = 0; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    void loadNam (const juce::File&);
    void loadCabIr (const juce::File&);
    void choose (bool nam);
    void setChoice (const char* id, int index);

    SwarmnessAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    juce::Rectangle<float> ampArea, cabArea, morphArea, curveArea, plateArea;

    PowerButton ampPower, cabPower;
    SegmentedChoice channelSelector, cabSelector;
    Knob gainKnob { "GAIN" }, bassKnob { "BASS" }, midKnob { "MID" }, trebleKnob { "TREBLE" };
    Knob presenceKnob { "PRESENCE" }, depthKnob { "DEPTH" }, masterKnob { "MASTER" }, gateKnob { "GATE" }, levelKnob { "LEVEL", true };
    Knob namInputKnob { "INPUT" }, namBassKnob { "BASS" }, namMidKnob { "MID" }, namTrebleKnob { "TREBLE" };
    Knob namPresenceKnob { "PRESENCE" }, namDepthKnob { "DEPTH" }, namOutputKnob { "OUTPUT" };
    juce::TextButton loadNamButton { "LOAD .NAM" }, toneButton { "TONE3000" }, clearNamButton { "CLEAR" };
    Knob micKnob { "MIC" }, distKnob { "DISTANCE" }, lowCutKnob { "LOW CUT" }, highCutKnob { "HIGH CUT" }, cabLevelKnob { "LEVEL", true };
    juce::TextButton loadIrButton { "LOAD IR" }, clearIrButton { "CLEAR" }, irToneButton { "TONE3000" };
    juce::TextButton namPrev { "<" }, namNext { ">" }, irPrev { "<" }, irNext { ">" }, irPrev2 { "<" }, irNext2 { ">" };
    Knob irMixKnob { "A / B MIX" };
    PillToggle irInvToggle { "INV B" };
    juce::Rectangle<float> irRowA, irRowB;
    int irSlot = 0;   // the IR slot LOAD IR / TONE3000 / CLEAR act on
    juce::String cabIrDescription2;
    PillToggle namLiteToggle { "LITE" };
    juce::String toneStatus;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> chooser;

    bool ampOn = false, cabOn = false, namMode = false, cabIrMode = false;
    int channel = -1, cabType = -1, dragTarget = 0;   // dragTarget: 1 = AMP, 2 = CAB
    float mic = -1.0f, dist = -1.0f;
    juce::String namDescription, cabIrDescription, message;
    int messageTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpCabSection)
};

//==============================================================================
/**
 * WASP (overdrive): VOLUME / DRIVE / BRIGHT / ATTACK / GATE, with the pedal's frequency response
 * and its clipping curve drawn live. NAM mode runs a pedal capture (.nam) instead: INPUT / OUTPUT /
 * GATE and the capture card (load, TONE3000, < >, LITE for A2).
 */
class WaspSection : public juce::Component,
                    public juce::FileDragAndDropTarget
{
public:
    explicit WaspSection (SwarmnessAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    DriveBlock::Settings current() const;
    void loadNam (const juce::File&);
    void updateVisibility();

    SwarmnessAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    juce::Rectangle<float> panelArea, responseArea, clipArea, cardArea;
    PowerButton power;
    SegmentedChoice modeSelector;
    Knob volumeKnob { "VOLUME" }, driveKnob { "DRIVE" }, brightKnob { "BRIGHT" }, attackKnob { "ATTACK" }, gateKnob { "GATE" };
    Knob namInputKnob { "INPUT" }, namOutputKnob { "OUTPUT" };
    juce::TextButton loadButton { "LOAD .NAM" }, toneButton { "TONE3000" }, clearButton { "CLEAR" }, prevButton { "<" }, nextButton { ">" };
    PillToggle liteToggle { "LITE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment, liteAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    DriveBlock::Settings shown;
    bool shownValid = false, namMode = false, dragOver = false;
    juce::String description, message, toneStatus;
    int messageTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaspSection)
};
