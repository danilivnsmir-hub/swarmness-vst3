#include "ChainStrip.h"

using namespace Theme;

namespace
{
    constexpr float kInW = 34.0f, kOutW = 42.0f, kGap = 20.0f, kSplitGap = 26.0f, kMergeGap = 46.0f;
}

ChainStrip::ChainStrip (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setRepaintsOnMouseActivity (false);

    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mixKnob.getProperties().set ("bipolar", true);
    mixKnob.setTooltip ("Parallel MIX: balance of path A (upper) and path B (lower) at the merge. "
                        "Centre = both at half level (identical paths = unity). An empty path is the dry signal");
    mixKnob.setDoubleClickReturnValue (true, 50.0);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, Chain::parallelMixId, mixKnob);
    addChildComponent (mixKnob);
}

ChainStrip::~ChainStrip() = default;

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

//==============================================================================
ChainStrip::Geometry ChainStrip::computeGeometry (const Chain::Layout& l) const
{
    Geometry g;
    const float h = (float) getHeight();
    g.cableY = h * 0.5f;
    g.laneAY = h * 0.25f + 0.5f;
    g.laneBY = h * 0.75f - 0.5f;

    const auto plan = Chain::planFor (l);
    const int parCols = plan.hasParallel() ? juce::jmax (1, plan.numA, plan.numB) : 0;
    const int cols = juce::jmax (1, plan.numPre + parCols + plan.numPost);

    std::vector<float> gaps ((size_t) cols + 1, kGap);
    if (plan.hasParallel())
    {
        gaps[(size_t) plan.numPre] = kSplitGap;
        gaps[(size_t) (plan.numPre + parCols)] = kMergeGap;
    }
    float gapSum = 0.0f;
    for (auto v : gaps)
        gapSum += v;
    const float tileW = ((float) getWidth() - kInW - kOutW - gapSum) / (float) cols;

    auto colX = [&] (int col)
    {
        float x = kInW;
        for (int c = 0; c < col; ++c)
            x += gaps[(size_t) c] + tileW;
        return x + gaps[(size_t) col];
    };

    const auto full = [&] (float x) { return juce::Rectangle<float> (x, 3.0f, tileW, h - 6.0f); };
    const auto upper = [&] (float x) { return juce::Rectangle<float> (x, 2.0f, tileW, h * 0.5f - 3.0f); };
    const auto lower = [&] (float x) { return juce::Rectangle<float> (x, h * 0.5f + 1.0f, tileW, h * 0.5f - 3.0f); };

    for (int i = 0; i < plan.numPre; ++i)
        g.rects[(size_t) plan.pre[(size_t) i]] = full (colX (i));
    for (int i = 0; i < plan.numA; ++i)
        g.rects[(size_t) plan.a[(size_t) i]] = upper (colX (plan.numPre + i));
    for (int i = 0; i < plan.numB; ++i)
        g.rects[(size_t) plan.b[(size_t) i]] = lower (colX (plan.numPre + i));
    for (int i = 0; i < plan.numPost; ++i)
        g.rects[(size_t) plan.post[(size_t) i]] = full (colX (plan.numPre + parCols + i));

    if (plan.hasParallel())
    {
        g.splitX = colX (plan.numPre) - kSplitGap * 0.5f;
        g.mergeX = colX (plan.numPre + parCols) - kMergeGap * 0.5f;
        g.emptyA = plan.numA == 0;
        g.emptyB = plan.numB == 0;
    }
    return g;
}

void ChainStrip::resized()
{
    geometry = computeGeometry (displayLayout());
    current = geometry.rects;
}

juce::Rectangle<float> ChainStrip::ledRect (juce::Rectangle<float> tile) const noexcept
{
    const bool half = isHalf (tile, (float) getHeight());
    const float s = half ? 10.0f : 14.0f;
    return juce::Rectangle<float> (s, s - 1.0f).withCentre ({ tile.getX() + (half ? 17.0f : 22.0f), tile.getCentreY() });
}

Chain::Layout ChainStrip::displayLayout() const
{
    if (! dragging || pressed < 0 || dragInsert < 0)
        return layout;
    Chain::Layout l = layout;
    int n = 0;
    for (int b : layout.order)
        if (b != pressed)
            l.order[(size_t) n++] = b;
    for (int i = Chain::numBlocks - 1; i > dragInsert; --i)
        l.order[(size_t) i] = l.order[(size_t) (i - 1)];
    l.order[(size_t) dragInsert] = pressed;
    l.lanes[(size_t) pressed] = dragLane;
    return l;
}

void ChainStrip::refresh()
{
    if (getLayout != nullptr && ! dragging)
        layout = getLayout();

    std::array<bool, Chain::numBlocks> now {};
    for (int b = 0; b < Chain::numBlocks; ++b)
        now[(size_t) b] = blockOn (b) || (isBlockActive != nullptr && isBlockActive (b));
    bool changed = now != lit;
    lit = now;

    geometry = computeGeometry (displayLayout());
    if (! initialised)
    {
        current = geometry.rects;
        initialised = true;
    }

    // ease the tiles towards their places
    for (int b = 0; b < Chain::numBlocks; ++b)
    {
        if (dragging && b == pressed)
            continue;
        auto& c = current[(size_t) b];
        const auto t = geometry.rects[(size_t) b];
        const float d = std::abs (c.getX() - t.getX()) + std::abs (c.getY() - t.getY())
                      + std::abs (c.getWidth() - t.getWidth()) + std::abs (c.getHeight() - t.getHeight());
        if (d > 0.8f)
        {
            c = juce::Rectangle<float> (c.getX() + 0.35f * (t.getX() - c.getX()), c.getY() + 0.35f * (t.getY() - c.getY()),
                                        c.getWidth() + 0.35f * (t.getWidth() - c.getWidth()), c.getHeight() + 0.35f * (t.getHeight() - c.getHeight()));
            changed = true;
        }
        else
        {
            c = t;
        }
    }

    const bool showMix = geometry.mergeX > 0.0f && ! dragging;
    if (showMix)
        mixKnob.setBounds (juce::Rectangle<int> (34, 34).withCentre ({ juce::roundToInt (geometry.mergeX), getHeight() / 2 }));
    if (mixKnob.isVisible() != showMix)
    {
        mixKnob.setVisible (showMix);
        changed = true;
    }

    if (changed)
        repaint();
}

int ChainStrip::blockAt (juce::Point<float> p) const
{
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (current[(size_t) b].contains (p))
            return b;
    return -1;
}

//==============================================================================
void ChainStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto& geo = geometry;
    const float cy = geo.cableY;
    const auto cable = Colours::accentDeep.withAlpha (0.6f);

    g.setFont (displayFont (17.0f));
    g.setColour (Colours::textDim);
    g.drawText ("IN", juce::Rectangle<float> (0.0f, 0.0f, 30.0f, bounds.getHeight()), juce::Justification::centredLeft, false);
    g.drawText ("OUT", juce::Rectangle<float> (bounds.getRight() - 40.0f, 0.0f, 40.0f, bounds.getHeight()), juce::Justification::centredRight, false);

    auto arrow = [&g] (float x, float y)
    {
        juce::Path a;
        a.addTriangle (x - 4.0f, y - 5.0f, x - 4.0f, y + 5.0f, x + 4.0f, y);
        g.setColour (Colours::accent.withAlpha (0.8f));
        g.fillPath (a);
    };

    // Cables
    g.setColour (cable);
    const float left = 26.0f, right = bounds.getRight() - 34.0f;
    if (geo.splitX < 0.0f)
    {
        g.fillRect (juce::Rectangle<float> (left, cy - 1.0f, right - left, 2.0f));
    }
    else
    {
        g.fillRect (juce::Rectangle<float> (left, cy - 1.0f, geo.splitX - left, 2.0f));
        g.fillRect (juce::Rectangle<float> (geo.mergeX, cy - 1.0f, right - geo.mergeX, 2.0f));
        // split / merge: the two paths
        for (float y : { geo.laneAY, geo.laneBY })
            g.fillRect (juce::Rectangle<float> (geo.splitX, y - 1.0f, geo.mergeX - geo.splitX, 2.0f));
        g.fillRect (juce::Rectangle<float> (geo.splitX - 1.0f, geo.laneAY, 2.0f, geo.laneBY - geo.laneAY));
        g.fillRect (juce::Rectangle<float> (geo.mergeX - 1.0f, geo.laneAY, 2.0f, geo.laneBY - geo.laneAY));

        g.setFont (font (10.5f, true));
        g.setColour (Colours::textFaint);
        g.drawText ("A", juce::Rectangle<float> (geo.splitX + 2.0f, geo.laneAY - 13.0f, 10.0f, 11.0f), juce::Justification::centredLeft, false);
        g.drawText ("B", juce::Rectangle<float> (geo.splitX + 2.0f, geo.laneBY + 2.0f, 10.0f, 11.0f), juce::Justification::centredLeft, false);

        // an empty path carries the dry signal
        g.setFont (font (11.5f, true));
        auto dryLabel = [&] (float y)
        {
            const auto r = juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ (geo.splitX + geo.mergeX) * 0.5f, y });
            g.setColour (Colours::background);
            g.fillRect (r.withSizeKeepingCentre (38.0f, 12.0f));
            g.setColour (Colours::textDim);
            g.drawText ("DRY", r, juce::Justification::centred, false);
        };
        if (geo.emptyA) dryLabel (geo.laneAY);
        if (geo.emptyB) dryLabel (geo.laneBY);
    }

    // Arrow heads on the main cable (in front of every series tile and at the output)
    for (int b = 0; b < Chain::numBlocks; ++b)
    {
        const auto& r = geo.rects[(size_t) b];
        if (! isHalf (r, bounds.getHeight()))
            arrow (r.getX() - kGap * 0.5f, cy);
    }
    arrow (right - 6.0f, cy);
    if (geo.splitX > 0.0f)
        arrow (geo.splitX - 7.0f, cy);

    auto drawTile = [&] (int b, juce::Rectangle<float> r, bool floating)
    {
        const bool half = isHalf (r, bounds.getHeight());
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
            g.fillPath (hexagon (led.expanded (3.0f)));
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

        // name (+ subtitle on full-height tiles)
        auto text = r.withTrimmedLeft (half ? 28.0f : 36.0f).withTrimmedRight (8.0f);
        if (half)
        {
            g.setFont (displayFont (16.0f));
            if (on || hi) g.setGradientFill (honeyGradient (text)); else g.setColour (Colours::textDim);
            g.drawText (Chain::names[b], text.translated (0.0f, 1.0f), juce::Justification::centredLeft, false);
            return;
        }
        g.setFont (displayFont (19.0f));
        const auto nameArea = text.withTrimmedBottom (text.getHeight() * 0.42f).translated (0.0f, 2.0f);
        if (on || hi) g.setGradientFill (honeyGradient (nameArea)); else g.setColour (Colours::textDim);
        g.drawText (Chain::names[b], nameArea, juce::Justification::bottomLeft, false);
        g.setFont (font (11.0f, true));
        g.setColour (hi ? Colours::text.withAlpha (0.8f) : Colours::textFaint);
        g.drawText (Chain::subtitles[b], text.withTrimmedTop (text.getHeight() * 0.58f), juce::Justification::topLeft, false);
    };

    for (int b = 0; b < Chain::numBlocks; ++b)
        if (! (dragging && b == pressed))
            drawTile (b, current[(size_t) b], false);

    if (dragging && pressed >= 0)
    {
        // where it will land, then the lifted tile
        const auto target = geo.rects[(size_t) pressed];
        g.setColour (Colours::accentBright.withAlpha (0.12f));
        g.fillPath (hexCapsule (target));
        g.setColour (Colours::accentBright.withAlpha (0.7f));
        g.strokePath (hexCapsule (target), juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        drawTile (pressed, target.withPosition (dragPos - grabOffset), true);

        g.setFont (font (11.0f, true));
        g.setColour (Colours::accentBright);
        const char* where = dragLane == Chain::pathA ? "PARALLEL A" : (dragLane == Chain::pathB ? "PARALLEL B" : "SERIES");
        g.drawText (where, juce::Rectangle<float> (target.getX(), target.getY() - 1.0f, target.getWidth(), 11.0f), juce::Justification::centredTop, false);
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
    setMouseCursor (h >= 0 ? (powerParamFor (h) != nullptr && ledRect (current[(size_t) h]).expanded (4.0f).contains (e.position)
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
    const auto r = current[(size_t) pressed];
    pressedOnLed = powerParamFor (pressed) != nullptr && ledRect (r).expanded (4.0f).contains (e.position);
    grabOffset = e.position - r.getPosition();
    dragPos = e.position;
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || pressedOnLed)
        return;
    if (! dragging && e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    dragPos = e.position;

    // lane from the height: up = path A, down = path B, middle = series
    const float h = (float) getHeight();
    dragLane = e.position.y < h * 0.28f ? Chain::pathA : (e.position.y > h * 0.72f ? Chain::pathB : Chain::series);

    // position: how many of the other tiles lie to the left of the dragged tile's centre
    const float cx = e.position.x - grabOffset.x + current[(size_t) pressed].getWidth() * 0.5f;
    Chain::Layout base = layout;
    int n = 0;
    for (int b : layout.order)
        if (b != pressed)
            base.order[(size_t) n++] = b;
    const auto baseGeo = computeGeometry (base);
    int insert = 0;
    for (int i = 0; i < Chain::numBlocks - 1; ++i)
        if (baseGeo.rects[(size_t) base.order[(size_t) i]].getCentreX() < cx)
            insert = i + 1;
    dragInsert = insert;

    refresh();
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (pressed < 0)
        return;

    if (dragging)
    {
        const auto newLayout = displayLayout();
        current[(size_t) pressed] = geometry.rects[(size_t) pressed].withPosition (dragPos - grabOffset);   // animate from the drop point
        dragging = false;
        if (newLayout != layout && setLayout != nullptr)
        {
            layout = newLayout;
            setLayout (newLayout);
        }
    }
    else if (pressedOnLed && ledRect (current[(size_t) pressed]).expanded (4.0f).contains (e.position))
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
        return "The signal chain, left to right. Drag a block sideways to move it, up / down to run it in parallel "
               "(path A / B), back to the middle for series. Click a block to open its controls";

    static const char* what[Chain::numBlocks] {
        "PITCH: STING octaves (footswitches) + HIVE harmonies. LED = HIVE on or an octave engaged",
        "SMOKE: fuzz",
        "SWARM: chorus",
        "WINGS: tremolo / stutter gate",
        "COMB: 10-band graphic EQ",
        "CARVE: parametric EQ with low / high cut",
        "CRYPT: reverb (algorithmic or your impulse response)" };
    juce::String tip (what[hover]);
    tip << ". Click to open; drag sideways to move, up / down for parallel paths";
    if (powerParamFor (hover) != nullptr)
        tip << "; LED = on / off";
    return tip;
}
