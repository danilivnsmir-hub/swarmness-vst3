#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

/**
 * Colours, fonts and drawing helpers shared by every GUI component.
 * Look: "necro-bees" after the logo - charred black, honey-to-venom-orange gradients,
 * bone text, honeycomb and grime.
 */
namespace Theme
{
    namespace Colours
    {
        inline const juce::Colour background   { 0xff0a0807 };
        inline const juce::Colour backgroundHi { 0xff1a120c };
        inline const juce::Colour panel        { 0xff120e0b };
        inline const juce::Colour panelHi      { 0xff1d1610 };
        inline const juce::Colour panelBorder  { 0xff3b2815 };
        inline const juce::Colour inset        { 0xff070504 };
        inline const juce::Colour accent       { 0xffff8a0a };   // venom orange
        inline const juce::Colour accentBright { 0xffffc93a };   // honey
        inline const juce::Colour accentDeep   { 0xffc2410c };   // dried blood-orange
        inline const juce::Colour text         { 0xffeee1c4 };   // bone
        inline const juce::Colour textDim      { 0xff9e8a6c };
        inline const juce::Colour textFaint    { 0xff5f503d };
        inline const juce::Colour ledGreen     { 0xff9cff4c };
        inline const juce::Colour ledRed       { 0xffff2d1a };
        inline const juce::Colour venom        { 0xffb05cff };
        inline const juce::Colour meterLow     { 0xffc26a00 };
        inline const juce::Colour meterMid     { 0xffffc93a };
        inline const juce::Colour meterHigh    { 0xffff2d1a };
    }

    inline juce::Typeface::Ptr typefaceMedium()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniMedium_ttf,
                                                                  BinaryData::RajdhaniMedium_ttfSize);
        return tf;
    }

    inline juce::Typeface::Ptr typefaceBold()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniBold_ttf,
                                                                  BinaryData::RajdhaniBold_ttfSize);
        return tf;
    }

    inline juce::Typeface::Ptr typefaceDisplay()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::MetalManiaRegular_ttf,
                                                                  BinaryData::MetalManiaRegular_ttfSize);
        return tf;
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (bold ? typefaceBold() : typefaceMedium()).withHeight (height));
    }

    /** Spiky metal lettering for titles (Metal Mania, SIL OFL). */
    inline juce::Font displayFont (float height)
    {
        return juce::Font (juce::FontOptions (typefaceDisplay()).withHeight (height));
    }

    /** Honey -> venom gradient across an area (vertical). */
    inline juce::ColourGradient honeyGradient (juce::Rectangle<float> r, float alpha = 1.0f)
    {
        juce::ColourGradient grad (Colours::accentBright.withAlpha (alpha), r.getX(), r.getY(),
                                   Colours::accentDeep.withAlpha (alpha), r.getX(), r.getBottom(), false);
        grad.addColour (0.55, Colours::accent.withAlpha (alpha));
        return grad;
    }

    /** Flat-topped hexagon inscribed in r. */
    inline juce::Path hexagon (juce::Rectangle<float> r, bool pointyTop = false)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float rx = r.getWidth() * 0.5f, ry = r.getHeight() * 0.5f;
        for (int i = 0; i < 6; ++i)
        {
            const float a = juce::MathConstants<float>::pi / 3.0f * (float) i + (pointyTop ? juce::MathConstants<float>::pi / 6.0f : 0.0f);
            const juce::Point<float> pt (c.x + rx * std::cos (a), c.y + ry * std::sin (a));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        p.closeSubPath();
        return p;
    }

    /** Capsule with pointed (hexagonal) ends - used for pills and segmented selectors. */
    inline juce::Path hexCapsule (juce::Rectangle<float> r)
    {
        const float t = r.getHeight() * 0.42f;
        juce::Path p;
        p.startNewSubPath (r.getX() + t, r.getY());
        p.lineTo (r.getRight() - t, r.getY());
        p.lineTo (r.getRight(), r.getCentreY());
        p.lineTo (r.getRight() - t, r.getBottom());
        p.lineTo (r.getX() + t, r.getBottom());
        p.lineTo (r.getX(), r.getCentreY());
        p.closeSubPath();
        return p;
    }

    /** Rectangle with chamfered corners (armoured plate). */
    inline juce::Path chamfered (juce::Rectangle<float> r, float c)
    {
        juce::Path p;
        p.startNewSubPath (r.getX() + c, r.getY());
        p.lineTo (r.getRight() - c, r.getY());
        p.lineTo (r.getRight(), r.getY() + c);
        p.lineTo (r.getRight(), r.getBottom() - c);
        p.lineTo (r.getRight() - c, r.getBottom());
        p.lineTo (r.getX() + c, r.getBottom());
        p.lineTo (r.getX(), r.getBottom() - c);
        p.lineTo (r.getX(), r.getY() + c);
        p.closeSubPath();
        return p;
    }

    /** Honeycomb outline pattern over an area (flat-topped cells of the given radius). */
    inline void drawHoneycomb (juce::Graphics& g, juce::Rectangle<float> area, float radius, juce::Colour colour, float thickness)
    {
        const float w = radius * 1.5f, h = radius * std::sqrt (3.0f);
        juce::Path cells;
        int col = 0;
        for (float x = area.getX() - radius; x < area.getRight() + radius; x += w, ++col)
            for (float y = area.getY() - h + ((col & 1) ? h * 0.5f : 0.0f); y < area.getBottom() + h; y += h)
                cells.addPath (hexagon (juce::Rectangle<float> (radius * 2.0f, h).withCentre ({ x, y })));
        g.setColour (colour);
        g.strokePath (cells, juce::PathStrokeType (thickness));
    }

    /** Deterministic grime: specks and scratches, seeded by the area so it never flickers. */
    inline void drawGrime (juce::Graphics& g, juce::Rectangle<float> r, float density = 1.0f)
    {
        juce::Random rng ((juce::int64) (r.getX() * 131.0f + r.getY() * 977.0f + r.getWidth()));
        const int specks = (int) (r.getWidth() * r.getHeight() / 900.0f * density);
        for (int i = 0; i < specks; ++i)
        {
            const float x = r.getX() + rng.nextFloat() * r.getWidth();
            const float y = r.getY() + rng.nextFloat() * r.getHeight();
            const float s = 0.6f + rng.nextFloat() * 1.6f;
            g.setColour ((rng.nextBool() ? juce::Colours::black : Colours::accent).withAlpha (0.05f + rng.nextFloat() * 0.07f));
            g.fillEllipse (x, y, s, s);
        }
        const int scratches = (int) (r.getWidth() / 90.0f * density);
        for (int i = 0; i < scratches; ++i)
        {
            const float x = r.getX() + rng.nextFloat() * r.getWidth();
            const float y = r.getY() + rng.nextFloat() * r.getHeight();
            const float len = 8.0f + rng.nextFloat() * 30.0f;
            const float a = -0.6f + rng.nextFloat() * 1.2f;
            g.setColour (Colours::text.withAlpha (0.025f + rng.nextFloat() * 0.03f));
            g.drawLine (x, y, x + len * std::cos (a), y + len * std::sin (a), 0.7f);
        }
    }

    /** Armoured, grimy plate with a faint honeycomb and a scorched honey border. */
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 10.0f)
    {
        const auto plate = chamfered (r, corner);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 18, { 0, 6 }).drawForPath (g, plate);

        g.setGradientFill (juce::ColourGradient (Colours::panelHi.withAlpha (0.94f), r.getX(), r.getY(),
                                                 Colours::panel.withAlpha (0.94f), r.getX(), r.getBottom(), false));
        g.fillPath (plate);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plate);
            drawHoneycomb (g, r, 16.0f, Colours::accent.withAlpha (0.035f), 1.0f);
            // warm glow from the top edge
            g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.07f), r.getCentreX(), r.getY(),
                                                     Colours::accent.withAlpha (0.0f), r.getCentreX(), r.getY() + 70.0f, false));
            g.fillRect (r.withHeight (70.0f));
            drawGrime (g, r);
        }

        // Border: scorched honey at the top fading into rust
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.55f), r.getX(), r.getY(),
                                                 Colours::panelBorder, r.getX(), r.getY() + 60.0f, false));
        g.strokePath (plate, juce::PathStrokeType (1.2f));

        // Corner notches
        g.setColour (Colours::accentBright.withAlpha (0.7f));
        g.drawLine (r.getX() + 1.0f, r.getY() + corner + 7.0f, r.getX() + 1.0f, r.getY() + corner, 2.0f);
        g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getX() + corner + 7.0f, r.getY() + 1.0f, 2.0f);
        g.drawLine (r.getRight() - 1.0f, r.getY() + corner + 7.0f, r.getRight() - 1.0f, r.getY() + corner, 2.0f);
        g.drawLine (r.getRight() - corner, r.getY() + 1.0f, r.getRight() - corner - 7.0f, r.getY() + 1.0f, 2.0f);
    }

    /** Section title: hex LED + spiky metal lettering in the honey gradient when active. */
    inline void drawSectionTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, bool active)
    {
        const auto led = area.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 11.0f);
        if (active)
        {
            g.setColour (Colours::accent.withAlpha (0.35f));
            g.fillPath (hexagon (led.expanded (4.0f)));
            g.setGradientFill (honeyGradient (led));
            g.fillPath (hexagon (led));
        }
        else
        {
            g.setColour (Colours::textFaint);
            g.strokePath (hexagon (led), juce::PathStrokeType (1.2f));
        }
        area.removeFromLeft (8.0f);

        g.setFont (displayFont (23.0f));
        const auto textArea = area.translated (0.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawText (title, textArea.translated (1.0f, 2.0f), juce::Justification::centredLeft, false);
        if (active)
            g.setGradientFill (honeyGradient (textArea.withSizeKeepingCentre (textArea.getWidth(), 20.0f)));
        else
            g.setColour (Colours::textDim);
        g.drawText (title, textArea, juce::Justification::centredLeft, false);
    }
}
