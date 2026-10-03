#pragma once

#include "Theme.h"
#include "../Parameters.h"

/**
 * The signal chain as hex tiles:  IN > [SMOKE] > [PITCH] > ... > OUT
 * Blocks can also run in parallel: the chain splits into path A (upper row) and path B
 * (lower row) and merges again (A/B mix knob at each merge). There can be several splits,
 * separated by series blocks. An empty path = dry signal.
 *  - click a tile: open the page with that block
 *  - drag a tile sideways: move it in the chain; drag it up / down: parallel path A / B;
 *    drag it back to the middle: series again
 *  - click the LED: switch the block on / off
 * Under the tiles a thin band names the zones: PRE (in front of the amp), RIG (AMP + CAB), POST.
 * A tile's second line is a live value (the amp model, the cabinet, WASP's character...) and
 * coloured dots mark the blocks a stomp (VENOM / STING) switches.
 */
class ChainStrip : public juce::Component,
                   public juce::TooltipClient
{
public:
    explicit ChainStrip (juce::AudioProcessorValueTreeState&);
    ~ChainStrip() override;

    std::function<Chain::Layout()> getLayout;
    std::function<void (const Chain::Layout&)> setLayout;
    std::function<void (int block)> onBlockClicked;
    std::function<void (int block)> onBlockRightClick;   // MIDI learn for the block's on / off
    /** Extra activity (e.g. STING engaged by a footswitch) that lights a tile without its power param. */
    std::function<bool (int block)> isBlockActive;
    /** The tile's second line (empty = the block's function, Chain::subtitles). */
    std::function<juce::String (int block)> subtitleFor;
    /** Stomp wiring of a block: bit 0 = VENOM switches it, bit 1 = STING. */
    std::function<int (int block)> stompMarksFor;

    /** Height of the zone band (PRE / RIG / POST) under the tiles. */
    static constexpr float zoneBandHeight = 15.0f;

    /** Blocks shown on the current page get a bright outline. */
    void setHighlighted (const std::array<bool, Chain::numBlocks>&);

    /** Called from the editor timer: pulls the layout / states and animates the tiles. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    /** The power parameter of a block. */
    static const char* powerParamFor (int block);

private:
    using Rects = std::array<juce::Rectangle<float>, Chain::numBlocks>;
    struct Split
    {
        float splitX = 0.0f, mergeX = 0.0f;
        bool emptyA = false, emptyB = false;
        int index = 0;   // its MIX parameter
    };
    struct Geometry
    {
        Rects rects;
        std::vector<Split> splits;
        float cableY = 0.0f, laneAY = 0.0f, laneBY = 0.0f;
    };

    Geometry computeGeometry (const Chain::Layout&) const;
    Chain::Layout displayLayout() const;   // the layout while dragging (preview)
    juce::Rectangle<float> ledRect (juce::Rectangle<float> tile) const noexcept;
    float tileAreaHeight() const noexcept { return juce::jmax (10.0f, (float) getHeight() - zoneBandHeight); }
    void paintZones (juce::Graphics&) const;
    int blockAt (juce::Point<float>) const;
    bool blockOn (int block) const;
    static bool isHalf (juce::Rectangle<float> r, float fullHeight) { return r.getHeight() < fullHeight * 0.7f; }

    juce::AudioProcessorValueTreeState& state;
    Chain::Layout layout { Chain::defaultOrder(), {} };
    Geometry geometry;                 // targets for the shown layout
    Rects current {};                  // animated tile rectangles
    std::array<bool, Chain::numBlocks> lit {}, highlighted {};
    std::array<juce::String, Chain::numBlocks> subtitles;
    std::array<int, Chain::numBlocks> stompMarks {};
    bool initialised = false;

    int hover = -1, pressed = -1, dragInsert = -1, dragLane = Chain::series;
    bool dragging = false, pressedOnLed = false;
    juce::Point<float> grabOffset, dragPos;

    std::array<juce::Slider, Chain::maxSplits> mixKnobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, Chain::maxSplits> mixAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};
