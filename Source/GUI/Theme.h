#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

/** Colours, fonts and small drawing helpers shared by every GUI component. */
namespace Theme
{
    namespace Colours
    {
        inline const juce::Colour background   { 0xff0e0f12 };
        inline const juce::Colour backgroundHi { 0xff17191e };
        inline const juce::Colour panel        { 0xff1a1c21 };
        inline const juce::Colour panelHi      { 0xff22252b };
        inline const juce::Colour panelBorder  { 0xff2c3038 };
        inline const juce::Colour inset        { 0xff0b0c0e };
        inline const juce::Colour accent       { 0xffff7a1a };
        inline const juce::Colour accentBright { 0xffffa04d };
        inline const juce::Colour accentDeep   { 0xffd24a00 };
        inline const juce::Colour text         { 0xffe9e9ec };
        inline const juce::Colour textDim      { 0xff8b919c };
        inline const juce::Colour textFaint    { 0xff5a606b };
        inline const juce::Colour ledGreen     { 0xff4cff9a };
        inline const juce::Colour ledRed       { 0xffff3b3b };
        inline const juce::Colour meterLow     { 0xff38d27a };
        inline const juce::Colour meterMid     { 0xffffc23d };
        inline const juce::Colour meterHigh    { 0xffff4a3d };
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

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (bold ? typefaceBold() : typefaceMedium()).withHeight (height));
    }

    /** Recessed panel with a soft top highlight. */
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 10.0f)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 14, { 0, 4 }).drawForPath (g, [&]
        {
            juce::Path p; p.addRoundedRectangle (r, corner); return p;
        }());

        g.setGradientFill (juce::ColourGradient (Colours::panelHi, r.getX(), r.getY(),
                                                 Colours::panel, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);

        g.setColour (Colours::panelBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
    }

    /** Section title with a small accent bar. */
    inline void drawSectionTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, bool active)
    {
        const auto bar = area.removeFromLeft (4.0f).withSizeKeepingCentre (4.0f, 14.0f);
        g.setColour (active ? Colours::accent : Colours::textFaint);
        g.fillRoundedRectangle (bar, 2.0f);
        area.removeFromLeft (8.0f);
        g.setFont (font (17.0f, true));
        g.setColour (active ? Colours::text : Colours::textDim);
        g.drawText (title, area, juce::Justification::centredLeft, false);
    }
}
