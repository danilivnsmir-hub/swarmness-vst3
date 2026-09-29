#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "Skin.h"

/**
 * The AMP's faceplates: each channel's amp gets its own panel behind its knobs and a nameplate with
 * a little head, so switching CLEAN / CRUNCH / LEAD / NAM looks like switching amps.
 *
 * Everything is drawn in code. Artwork (Skin.h) replaces it without touching code:
 * amp_plate_<style>.png (the panel behind the knobs, 220 px high, drawn 9-slice) and
 * amp_head_<style>.png (the head on the nameplate, 300 x 200 px, transparent), style = chrome, brit,
 * steel or nam.
 */
namespace AmpFaceplate
{
    enum Style { chrome = 0, brit, steel, nam };

    inline const char* styleName (int style)
    {
        static const char* names[] { "chrome", "brit", "steel", "nam" };
        return names[juce::jlimit (0, 3, style)];
    }

    inline void screw (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour metal, float angle)
    {
        g.setGradientFill (juce::ColourGradient (metal.brighter (0.6f), c.x - r * 0.5f, c.y - r * 0.6f, metal.darker (0.8f), c.x + r, c.y + r, true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 0.8f);
        const juce::Point<float> d (std::cos (angle) * r * 0.75f, std::sin (angle) * r * 0.75f);
        g.drawLine (juce::Line<float> (c - d, c + d), 1.3f);
    }

    inline void hexBolt (juce::Graphics& g, juce::Point<float> c, float r)
    {
        const auto area = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8c8f94), area.getX(), area.getY(), juce::Colour (0xff2a2c30), area.getRight(), area.getBottom(), false));
        g.fillPath (Theme::hexagon (area, true));
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.strokePath (Theme::hexagon (area, true), juce::PathStrokeType (0.8f));
        g.setColour (juce::Colour (0xff15161a));
        g.fillEllipse (area.reduced (r * 0.55f));
    }

    /** The panel behind the knobs. */
    inline void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, int style, bool on)
    {
        const float corner = 7.0f;
        juce::Path shape;
        shape.addRoundedRectangle (r, corner);

        // artwork: its own frame and hardware stay put (9-slice), slim and pushed out a little so the
        // knob captions and values sit on the plate, not on the frame; off = a shade darker
        if (Skin::drawNine (g, juce::String ("amp_plate_") + styleName (style), r.expanded (5.0f), 44.0f, 0.45f))
        {
            if (! on)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRoundedRectangle (r.reduced (2.0f), corner);
            }
            return;
        }

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (shape);
            juce::Random rng (17 + style);
            switch (style)
            {
                case chrome:   // dark brushed steel
                {
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2f3236), r.getX(), r.getY(), juce::Colour (0xff17181a), r.getX(), r.getBottom(), false));
                    g.fillRect (r);
                    for (float y = r.getY(); y < r.getBottom(); y += 1.0f)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.035f * rng.nextFloat()));
                        const float x0 = r.getX() + r.getWidth() * rng.nextFloat() * 0.3f;
                        g.fillRect (juce::Rectangle<float> (x0, y, r.getWidth() * (0.5f + 0.5f * rng.nextFloat()), 0.6f));
                    }
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), r.getCentreX(), r.getY(),
                                                             juce::Colours::transparentWhite, r.getCentreX(), r.getY() + r.getHeight() * 0.45f, false));
                    g.fillRect (r);
                    break;
                }
                case brit:     // black levant vinyl
                {
                    g.fillAll (juce::Colour (0xff141110));
                    for (int i = 0; i < (int) (r.getWidth() * r.getHeight() / 26.0f); ++i)
                    {
                        const float x = r.getX() + r.getWidth() * rng.nextFloat(), y = r.getY() + r.getHeight() * rng.nextFloat();
                        const float s = 1.2f + 2.2f * rng.nextFloat();
                        g.setColour (juce::Colours::white.withAlpha (0.018f + 0.03f * rng.nextFloat()));
                        g.fillEllipse (x, y, s * 1.3f, s);
                        g.setColour (juce::Colours::black.withAlpha (0.35f));
                        g.fillEllipse (x + 0.6f, y + 0.8f, s * 1.1f, s * 0.8f);
                    }
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), r.getCentreX(), r.getY(),
                                                             juce::Colours::transparentWhite, r.getCentreX(), r.getBottom(), false));
                    g.fillRect (r);
                    break;
                }
                case steel:    // diamond plate
                {
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff232427), r.getX(), r.getY(), juce::Colour (0xff111214), r.getRight(), r.getBottom(), false));
                    g.fillRect (r);
                    const float sx = 18.0f, sy = 14.0f;
                    int row = 0;
                    for (float y = r.getY() - sy; y < r.getBottom() + sy; y += sy, ++row)
                        for (float x = r.getX() - sx + (row % 2) * sx * 0.5f; x < r.getRight() + sx; x += sx)
                        {
                            juce::Path d;
                            d.addRoundedRectangle (-5.5f, -1.4f, 11.0f, 2.8f, 1.4f);
                            const float a = ((row + (int) (x / sx)) % 2 == 0 ? 0.62f : -0.62f);
                            d.applyTransform (juce::AffineTransform::rotation (a).translated (x, y));
                            g.setColour (juce::Colours::white.withAlpha (0.10f));
                            g.fillPath (d, juce::AffineTransform::translation (-0.6f, -0.6f));
                            g.setColour (juce::Colours::black.withAlpha (0.55f));
                            g.fillPath (d, juce::AffineTransform::translation (0.7f, 0.8f));
                            g.setColour (juce::Colour (0xff2b2d31));
                            g.fillPath (d);
                        }
                    break;
                }
                default:       // NAM: circuit traces
                {
                    g.fillAll (juce::Colour (0xff0e0b12));
                    for (int i = 0; i < 46; ++i)
                    {
                        float x = r.getX() + r.getWidth() * rng.nextFloat(), y = r.getY() + r.getHeight() * rng.nextFloat();
                        juce::Path p;
                        p.startNewSubPath (x, y);
                        for (int k = 0; k < 3; ++k)
                        {
                            if (rng.nextBool()) x += (rng.nextFloat() - 0.5f) * 120.0f; else y += (rng.nextFloat() - 0.5f) * 60.0f;
                            p.lineTo (x, y);
                        }
                        g.setColour (Theme::Colours::venom.withAlpha (0.10f + 0.12f * rng.nextFloat()));
                        g.strokePath (p, juce::PathStrokeType (1.0f));
                        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
                    }
                    break;
                }
            }
            // keep the knob labels readable
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRect (r);
        }

        // piping / edge and the hardware
        const auto piping = style == chrome ? juce::Colour (0xffb9bec4) : style == brit ? juce::Colour (0xffd9861c)
                          : style == steel ? Theme::Colours::accent : Theme::Colours::venom;
        g.setColour (piping.withAlpha (on ? 0.75f : 0.35f));
        g.drawRoundedRectangle (r.reduced (3.0f), corner - 2.0f, style == brit ? 2.0f : 1.4f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r, corner, 1.0f);
        const float in = 11.0f;
        for (auto c : { r.getTopLeft() + juce::Point<float> (in, in), r.getTopRight() + juce::Point<float> (-in, in),
                        r.getBottomLeft() + juce::Point<float> (in, -in), r.getBottomRight() + juce::Point<float> (-in, -in) })
        {
            if (style == steel) hexBolt (g, c, 4.5f);
            else if (style == chrome) screw (g, c, 3.6f, juce::Colour (0xff9aa0a6), 0.6f + c.x * 0.01f);
            else if (style == brit) screw (g, c, 3.6f, juce::Colour (0xff8c8f94), 0.3f + c.y * 0.02f);
        }
        if (style == steel)
        {
            g.setColour (Theme::Colours::accent.withAlpha (on ? 0.9f : 0.4f));
            g.fillRect (juce::Rectangle<float> (r.getX() + 24.0f, r.getBottom() - 6.0f, r.getWidth() - 48.0f, 2.0f));
        }
    }

    /** A small amp head (the nameplate's picture). */
    inline void drawHead (juce::Graphics& g, juce::Rectangle<float> r, int style, bool on)
    {
        if (Skin::drawFitted (g, juce::String ("amp_head_") + styleName (style), r, juce::RectanglePlacement::centred, on ? 1.0f : 0.6f))
            return;
        const auto body = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight() * 1.55f), r.getHeight() * 0.86f);
        const auto tolex = style == chrome ? juce::Colour (0xff1e2023) : style == brit ? juce::Colour (0xff15110e)
                         : style == steel ? juce::Colour (0xff111114) : juce::Colour (0xff120d18);
        const auto trim = style == chrome ? juce::Colour (0xffc4c9cf) : style == brit ? juce::Colour (0xffd9861c)
                        : style == steel ? Theme::Colours::accent : Theme::Colours::venom;
        // shadow, box
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (body.translated (3.0f, 4.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (tolex.brighter (0.25f), body.getX(), body.getY(), tolex.darker (0.4f), body.getX(), body.getBottom(), false));
        g.fillRoundedRectangle (body, 6.0f);
        g.setColour (trim.withAlpha (0.55f));
        g.drawRoundedRectangle (body.reduced (1.5f), 5.0f, 1.2f);
        // handle
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (juce::Rectangle<float> (body.getWidth() * 0.34f, 5.0f).withCentre ({ body.getCentreX(), body.getY() - 1.0f }), 2.5f);
        // grille (upper) and control panel (lower)
        auto inner = body.reduced (7.0f);
        auto panel = inner.removeFromBottom (inner.getHeight() * 0.36f);
        const auto grille = inner.withTrimmedBottom (3.0f);
        g.setColour (juce::Colour (0xff0b0a09));
        g.fillRoundedRectangle (grille, 3.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (grille.toNearestInt());
            g.setColour (trim.withAlpha (0.10f));
            for (float x = grille.getX() - grille.getHeight(); x < grille.getRight(); x += 3.0f)
                g.drawLine (x, grille.getBottom(), x + grille.getHeight(), grille.getY(), 0.8f);
        }
        // logo on the grille: a tiny hex
        g.setColour (trim.withAlpha (0.85f));
        g.fillPath (Theme::hexagon (juce::Rectangle<float> (9.0f, 8.0f).withCentre (grille.getCentre()), true));
        g.setGradientFill (juce::ColourGradient (trim.withAlpha (0.35f), panel.getX(), panel.getY(), trim.withAlpha (0.12f), panel.getX(), panel.getBottom(), false));
        g.fillRoundedRectangle (panel, 2.5f);
        const int knobs = 6;
        for (int i = 0; i < knobs; ++i)
        {
            const auto c = juce::Point<float> (panel.getX() + panel.getWidth() * (0.12f + 0.66f * (float) i / (knobs - 1)), panel.getCentreY());
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
            g.setColour (trim.withAlpha (0.7f));
            g.drawLine (c.x, c.y, c.x + 2.0f, c.y - 2.5f, 1.0f);
        }
        // the jewel
        const auto jewel = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ panel.getRight() - 9.0f, panel.getCentreY() });
        if (on)
        {
            g.setColour (Theme::Colours::ledRed.withAlpha (0.35f));
            g.fillEllipse (jewel.expanded (4.0f));
        }
        g.setColour (on ? Theme::Colours::ledRed : Theme::Colours::ledRed.darker (0.8f));
        g.fillEllipse (jewel);
    }

    /** The nameplate: the model's name in metal, what it is, and its head. */
    inline void drawNameplate (juce::Graphics& g, juce::Rectangle<float> card, int style, const juce::String& name,
                               const juce::String& about, bool on)
    {
        Theme::drawInset (g, card, 6.0f);
        auto t = card.reduced (18.0f, 12.0f);
        const auto head = t.removeFromRight (150.0f).withTrimmedTop (2.0f).withTrimmedBottom (2.0f);
        drawHead (g, head, style, on);
        t.removeFromRight (10.0f);

        const auto metalTop = style == chrome ? juce::Colour (0xfff2f4f7) : style == brit ? juce::Colour (0xfff0b25a) : Theme::Colours::accentBright;
        const auto metalBottom = style == chrome ? juce::Colour (0xff8c939b) : style == brit ? juce::Colour (0xff8a4f14) : Theme::Colours::accentDeep;
        auto titleArea = t.removeFromTop (40.0f);
        g.setFont (Theme::displayFont (34.0f));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawText (name, titleArea.translated (1.5f, 2.0f), juce::Justification::centredLeft, false);
        if (on)
            g.setGradientFill (juce::ColourGradient (metalTop, titleArea.getX(), titleArea.getY() + 6.0f, metalBottom, titleArea.getX(), titleArea.getBottom() - 4.0f, false));
        else
            g.setColour (Theme::Colours::textDim);
        g.drawText (name, titleArea, juce::Justification::centredLeft, false);

        g.setFont (Theme::font (13.5f));
        g.setColour (Theme::Colours::textDim);
        g.drawFittedText (about, t.toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }

    /** The CAB's panel: speaker grille cloth behind its knobs (`type` = CabBlock type: 0 1x12 OPEN,
        1 2x12 OPEN, 2 4x12 BRIT = salt & pepper weave, 3 4x12 MOD = black cloth, 4 IR). Artwork
        (Skin.h): cab_grille_<type>.png, 210 px high, drawn 9-slice. */
    inline void drawGrille (juce::Graphics& g, juce::Rectangle<float> r, int type, bool on)
    {
        const float corner = 7.0f;
        juce::Path shape;
        shape.addRoundedRectangle (r, corner);

        if (Skin::drawNine (g, "cab_grille_" + juce::String (type), r.expanded (4.0f), 24.0f, 0.6f))
        {
            // the cloth a shade darker under the light captions (the light weave most), more when off
            g.setColour (juce::Colours::black.withAlpha ((type == 2 ? 0.38f : 0.18f) + (on ? 0.0f : 0.3f)));
            g.fillRoundedRectangle (r.reduced (3.0f), corner);
            return;
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        {
            const bool brit = type == 2, open = type <= 1, ir = type >= 4;
            const juce::Colour base = brit ? juce::Colour (0xff2b2721) : open ? juce::Colour (0xff231d17) : juce::Colour (0xff141416);
            g.setGradientFill (juce::ColourGradient (base.brighter (0.25f), r.getCentreX(), r.getY(), base.darker (0.5f), r.getCentreX(), r.getBottom(), false));
            g.fillRect (r);
            // the weave: two sets of fine diagonals, the threads a little uneven
            juce::Random rng (1234 + type);
            const juce::Colour thread = brit ? juce::Colour (0xffb8ab8c) : open ? juce::Colour (0xff8a6a3e) : juce::Colour (0xff5a5a60);
            for (int dir = 0; dir < 2; ++dir)
                for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 3.0f)
                {
                    g.setColour (thread.withAlpha ((brit ? 0.10f : 0.07f) + 0.05f * rng.nextFloat()));
                    const float x2 = x + r.getHeight();
                    if (dir == 0) g.drawLine (x, r.getBottom(), x2, r.getY(), 0.8f);
                    else          g.drawLine (x, r.getY(), x2, r.getBottom(), 0.8f);
                }
            if (brit)   // salt & pepper flecks
                for (int k = 0; k < (int) (r.getWidth() * r.getHeight() / 60.0f); ++k)
                {
                    g.setColour ((rng.nextBool() ? juce::Colour (0xffd8ccb0) : juce::Colours::black).withAlpha (0.18f));
                    g.fillRect (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight(), 1.2f, 1.2f);
                }
            if (ir)     // IR: the cloth over a faint honeycomb baffle
            {
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                const float hs = 26.0f;
                for (float y = r.getY() - hs; y < r.getBottom() + hs; y += hs * 0.87f)
                    for (float x = r.getX() - hs; x < r.getRight() + hs; x += hs)
                    {
                        const float off = (int) ((y - r.getY()) / (hs * 0.87f)) % 2 == 0 ? 0.0f : hs * 0.5f;
                        g.strokePath (Theme::hexagon (juce::Rectangle<float> (hs * 0.9f, hs * 0.9f).withCentre ({ x + off, y }), true), juce::PathStrokeType (1.0f));
                    }
            }
            // the speakers showing through the cloth
            const int cones = type == 0 ? 1 : type == 1 ? 2 : 4;
            for (int k = 0; k < cones; ++k)
            {
                const float cx = r.getX() + r.getWidth() * ((float) k + 0.5f) / (float) cones;
                const float rad = juce::jmin (r.getHeight() * 0.62f, r.getWidth() / (float) cones * 0.46f);
                g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.30f), cx, r.getCentreY(),
                                                         juce::Colours::transparentBlack, cx + rad, r.getCentreY(), true));
                g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre ({ cx, r.getCentreY() }));
            }
        }
        // vignette + piping
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::black.withAlpha (0.45f), r.getX(), r.getY(), true));
        g.fillRect (r);
        if (! on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (r);
        }
        g.setColour (type == 2 ? juce::Colour (0xffd9861c).withAlpha (on ? 0.8f : 0.4f) : Theme::Colours::panelBorder.brighter (0.2f));
        g.strokePath (shape, juce::PathStrokeType (1.4f));
    }
}
