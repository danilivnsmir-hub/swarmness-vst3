#pragma once

#include "Theme.h"
#include "../Parameters.h"

/**
 * The signal chain as a row of hex tiles:  IN > [SMOKE] > [PITCH] > ... > OUT
 *  - click a tile: open the page with that block
 *  - drag a tile: move the block in the chain (the processor crossfades the change)
 *  - click the LED: switch the block on / off
 */
class ChainStrip : public juce::Component,
                   public juce::TooltipClient
{
public:
    explicit ChainStrip (juce::AudioProcessorValueTreeState&);

    std::function<Chain::Order()> getOrder;
    std::function<void (const Chain::Order&)> setOrder;
    std::function<void (int block)> onBlockClicked;
    /** Extra activity (e.g. STING engaged by a footswitch) that lights a tile without its power param. */
    std::function<bool (int block)> isBlockActive;

    /** Blocks shown on the current page get a bright outline. */
    void setHighlighted (const std::array<bool, Chain::numBlocks>&);

    /** Called from the editor timer: pulls the order / states and animates the tiles. */
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
    float slotX (int position) const noexcept { return firstX + (float) position * (tileW + gap); }
    juce::Rectangle<float> tileRect (float left) const noexcept { return { left, 3.0f, tileW, (float) getHeight() - 6.0f }; }
    juce::Rectangle<float> ledRect (juce::Rectangle<float> tile) const noexcept;
    int blockAt (juce::Point<float>) const;
    bool blockOn (int block) const;
    Chain::Order displayOrder() const;   // the order while dragging (preview)

    juce::AudioProcessorValueTreeState& state;
    Chain::Order order = Chain::defaultOrder();
    std::array<float, Chain::numBlocks> x {};           // animated tile positions per block
    std::array<bool, Chain::numBlocks> lit {}, highlighted {};
    float firstX = 40.0f, tileW = 120.0f, gap = 22.0f;

    int hover = -1, pressed = -1, dragInsert = -1;
    bool dragging = false, pressedOnLed = false, animating = false;
    float grabOffset = 0.0f, dragX = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};
