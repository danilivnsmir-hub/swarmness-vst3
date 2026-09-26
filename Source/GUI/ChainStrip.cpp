#include "ChainStrip.h"

using namespace Theme;

ChainStrip::ChainStrip (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setRepaintsOnMouseActivity (false);
}

const char* ChainStrip::powerParamFor (int block)
{
    switch (block)
    {
        case Chain::smoke: return ParamIDs::fuzzOn;
        case Chain::swarm: return ParamIDs::swarmOn;
        case Chain::wings: return ParamIDs::flowOn;
        case Chain::comb:  return ParamIDs::geqOn;
        case Chain::carve: return ParamIDs::peqOn;
        case Chain::crypt: return ParamIDs::revOn;
        default:           return nullptr;
    }
}

bool ChainStrip::blockOn (int block) const
{
    if (block == Chain::pitch)
        return state.getRawParameterValue (ParamIDs::rbOn)->load() > 0.5f;
    if (auto* id = powerParamFor (block))
        return state.getRawParameterValue (id)->load() > 0.5f;
    return false;
}

void ChainStrip::setHighlighted (const std::array<bool, Chain::numBlocks>& h)
{
    if (h != highlighted)
    {
        highlighted = h;
        repaint();
    }
}

void ChainStrip::resized()
{
    const float inW = 34.0f, outW = 42.0f;
    gap = 20.0f;
    firstX = inW + gap * 0.5f;
    tileW = ((float) getWidth() - inW - outW - gap * (float) Chain::numBlocks) / (float) Chain::numBlocks;
    for (int pos = 0; pos < Chain::numBlocks; ++pos)
        x[(size_t) order[(size_t) pos]] = slotX (pos);
}

juce::Rectangle<float> ChainStrip::ledRect (juce::Rectangle<float> tile) const noexcept
{
    return juce::Rectangle<float> (14.0f, 13.0f).withCentre ({ tile.getX() + 22.0f, tile.getCentreY() });
}

Chain::Order ChainStrip::displayOrder() const
{
    if (! dragging || pressed < 0 || dragInsert < 0)
        return order;
    Chain::Order o {};
    int n = 0;
    for (int b : order)
        if (b != pressed)
            o[(size_t) n++] = b;
    // insert the dragged block at dragInsert
    for (int i = Chain::numBlocks - 1; i > dragInsert; --i)
        o[(size_t) i] = o[(size_t) (i - 1)];
    o[(size_t) dragInsert] = pressed;
    return o;
}

void ChainStrip::refresh()
{
    if (getOrder != nullptr && ! dragging)
        order = getOrder();

    std::array<bool, Chain::numBlocks> now {};
    for (int b = 0; b < Chain::numBlocks; ++b)
        now[(size_t) b] = blockOn (b) || (isBlockActive != nullptr && isBlockActive (b));
    bool changed = now != lit;
    lit = now;

    // ease the tiles towards their slots
    const auto shown = displayOrder();
    animating = false;
    for (int pos = 0; pos < Chain::numBlocks; ++pos)
    {
        const int b = shown[(size_t) pos];
        if (dragging && b == pressed)
            continue;
        auto& cx = x[(size_t) b];
        const float target = slotX (pos);
        if (std::abs (cx - target) > 0.5f)
        {
            cx += 0.35f * (target - cx);
            animating = changed = true;
        }
        else
        {
            cx = target;
        }
    }

    if (changed)
        repaint();
}

int ChainStrip::blockAt (juce::Point<float> p) const
{
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (tileRect (x[(size_t) b]).contains (p))
            return b;
    return -1;
}

//==============================================================================
void ChainStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float cy = bounds.getCentreY();

    // The cable: IN .... OUT
    g.setColour (Colours::accentDeep.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (26.0f, cy - 1.0f, bounds.getWidth() - 60.0f, 2.0f));

    g.setFont (displayFont (17.0f));
    g.setColour (Colours::textDim);
    g.drawText ("IN", juce::Rectangle<float> (0.0f, 0.0f, 30.0f, bounds.getHeight()), juce::Justification::centredLeft, false);
    g.drawText ("OUT", juce::Rectangle<float> (bounds.getRight() - 40.0f, 0.0f, 40.0f, bounds.getHeight()), juce::Justification::centredRight, false);

    // Arrow heads between the slots
    for (int pos = 0; pos <= Chain::numBlocks; ++pos)
    {
        const float ax = pos == 0 ? firstX - gap * 0.5f : slotX (pos) - gap * 0.5f;
        juce::Path arrow;
        arrow.addTriangle (ax - 4.0f, cy - 5.0f, ax - 4.0f, cy + 5.0f, ax + 4.0f, cy);
        g.setColour (Colours::accent.withAlpha (0.8f));
        g.fillPath (arrow);
    }

    auto drawTile = [&] (int b, float tx, bool floating)
    {
        const auto r = tileRect (tx);
        const auto shape = hexCapsule (r);
        const bool on = lit[(size_t) b];
        const bool hi = highlighted[(size_t) b];
        const bool hov = hover == b && ! dragging;

        if (floating)
            juce::DropShadow (juce::Colours::black.withAlpha (0.9f), 14, { 0, 5 }).drawForPath (g, shape);

        g.setGradientFill (juce::ColourGradient (hi ? Colours::panelHi.brighter (0.25f) : Colours::panelHi, r.getX(), r.getY(),
                                                 Colours::inset, r.getX(), r.getBottom(), false));
        g.fillPath (shape);

        if (on)
        {
            g.setColour (Colours::accent.withAlpha (0.10f));
            g.fillPath (shape);
        }

        // outline: honey when the tile's page is open, dim otherwise
        if (hi)
        {
            g.setColour (Colours::accent.withAlpha (0.25f));
            g.strokePath (shape, juce::PathStrokeType (4.0f));
            g.setGradientFill (honeyGradient (r));
            g.strokePath (shape, juce::PathStrokeType (1.6f));
        }
        else
        {
            g.setColour ((hov || floating) ? Colours::accent.withAlpha (0.7f) : Colours::panelBorder.brighter (0.2f));
            g.strokePath (shape, juce::PathStrokeType (1.2f));
        }

        // power LED (PITCH: activity only)
        const auto led = ledRect (r);
        if (on)
        {
            g.setColour (Colours::accent.withAlpha (0.35f));
            g.fillPath (hexagon (led.expanded (3.5f)));
            g.setGradientFill (honeyGradient (led));
            g.fillPath (hexagon (led));
        }
        else
        {
            g.setColour (Colours::inset);
            g.fillPath (hexagon (led));
            g.setColour (Colours::textFaint);
            g.strokePath (hexagon (led), juce::PathStrokeType (1.2f));
        }

        // name + subtitle
        auto text = r.withTrimmedLeft (36.0f).withTrimmedRight (10.0f);
        g.setFont (displayFont (19.0f));
        const auto nameArea = text.withTrimmedBottom (text.getHeight() * 0.42f).translated (0.0f, 2.0f);
        if (on || hi)
            g.setGradientFill (honeyGradient (nameArea));
        else
            g.setColour (Colours::textDim);
        g.drawText (Chain::names[b], nameArea, juce::Justification::bottomLeft, false);
        g.setFont (font (11.0f, true));
        g.setColour (hi ? Colours::text.withAlpha (0.8f) : Colours::textFaint);
        g.drawText (Chain::subtitles[b], text.withTrimmedTop (text.getHeight() * 0.58f), juce::Justification::topLeft, false);
    };

    for (int b = 0; b < Chain::numBlocks; ++b)
        if (! (dragging && b == pressed))
            drawTile (b, x[(size_t) b], false);

    if (dragging && pressed >= 0)
    {
        // insertion marker + the lifted tile
        const float mx = slotX (juce::jmax (0, dragInsert)) - gap * 0.5f;
        g.setColour (Colours::accentBright.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (mx - 1.0f, 4.0f, 2.0f, bounds.getHeight() - 8.0f));
        drawTile (pressed, dragX, true);
    }
}

//==============================================================================
void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = blockAt (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
    setMouseCursor (h >= 0 ? (powerParamFor (h) != nullptr && ledRect (tileRect (x[(size_t) h])).expanded (4.0f).contains (e.position)
                                  ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor)
                           : juce::MouseCursor::NormalCursor);
}

void ChainStrip::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    pressed = blockAt (e.position);
    dragging = false;
    if (pressed < 0)
        return;
    const auto r = tileRect (x[(size_t) pressed]);
    pressedOnLed = powerParamFor (pressed) != nullptr && ledRect (r).expanded (4.0f).contains (e.position);
    grabOffset = e.position.x - r.getX();
    dragX = r.getX();
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || pressedOnLed)
        return;
    if (! dragging && e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    dragX = juce::jlimit (firstX - gap, slotX (Chain::numBlocks - 1) + gap, e.position.x - grabOffset);
    dragInsert = juce::jlimit (0, Chain::numBlocks - 1, juce::roundToInt ((dragX - firstX) / (tileW + gap)));
    refresh();
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (pressed < 0)
        return;

    if (dragging)
    {
        const auto newOrder = displayOrder();
        x[(size_t) pressed] = dragX;   // animate from where it was dropped
        dragging = false;
        if (newOrder != order && setOrder != nullptr)
        {
            order = newOrder;
            setOrder (newOrder);
        }
    }
    else if (pressedOnLed && ledRect (tileRect (x[(size_t) pressed])).expanded (4.0f).contains (e.position))
    {
        if (auto* param = state.getParameter (powerParamFor (pressed)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
    }
    else if (onBlockClicked != nullptr && blockAt (e.position) == pressed)
    {
        onBlockClicked (pressed);
    }

    pressed = -1;
    dragInsert = -1;
    refresh();
    repaint();
}

juce::String ChainStrip::getTooltip()
{
    if (hover < 0)
        return "The signal chain, left to right. Drag a block to move it; click it to open its controls";

    static const char* what[Chain::numBlocks] {
        "PITCH: STING octaves (footswitches) + HIVE harmonies. LED = HIVE on or an octave engaged",
        "SMOKE: fuzz",
        "SWARM: chorus",
        "WINGS: tremolo / stutter gate",
        "COMB: 10-band graphic EQ",
        "CARVE: parametric EQ with low / high cut",
        "CRYPT: reverb (algorithmic or your impulse response)" };
    juce::String tip (what[hover]);
    tip << ". Click to open, drag to move it in the chain";
    if (powerParamFor (hover) != nullptr)
        tip << ", LED = on / off";
    return tip;
}
