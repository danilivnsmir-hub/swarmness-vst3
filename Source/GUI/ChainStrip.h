#pragma once

#include "Theme.h"
#include "../Parameters.h"

/**
 * The signal chain as hex tiles:  IN > [SMOKE] > [PITCH] > ... > OUT
 * Blocks can also run in parallel: the chain splits into path A (upper row) and path B
 * (lower row) and merges again (A/B mix knob at the merge). An empty path = dry signal.
 *  - click a tile: open the page with that block
 *  - drag a tile sideways: move it in the chain; drag it up / down: parallel path A / B;
 *    drag it back to the middle: series again
 *  - click the LED: switch the block on / off
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
    /** Extra activity (e.g. STING engaged by a footswitch) that lights a tile without its power param. */
    std::function<bool (int block)> isBlockActive;

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

    /** The power parameter of a block (nullptr: PITCH has none - STING is played with the footswitches). */
    static const char* powerParamFor (int block);

private:
    using Rects = std::array<juce::Rectangle<float>, Chain::numBlocks>;
    struct Geometry
    {
        Rects rects;
        float splitX = -1.0f, mergeX = -1.0f;
        float cableY = 0.0f, laneAY = 0.0f, laneBY = 0.0f;
        bool emptyA = false, emptyB = false;
    };

    Geometry computeGeometry (const Chain::Layout&) const;
    Chain::Layout displayLayout() const;   // the layout while dragging (preview)
    juce::Rectangle<float> ledRect (juce::Rectangle<float> tile) const noexcept;
    int blockAt (juce::Point<float>) const;
    bool blockOn (int block) const;
    static bool isHalf (juce::Rectangle<float> r, float fullHeight) { return r.getHeight() < fullHeight * 0.7f; }

    juce::AudioProcessorValueTreeState& state;
    Chain::Layout layout { Chain::defaultOrder(), {} };
    Geometry geometry;                 // targets for the shown layout
    Rects current {};                  // animated tile rectangles
    std::array<bool, Chain::numBlocks> lit {}, highlighted {};
    bool initialised = false;

    int hover = -1, pressed = -1, dragInsert = -1, dragLane = Chain::series;
    bool dragging = false, pressedOnLed = false;
    juce::Point<float> grabOffset, dragPos;

    juce::Slider mixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};
