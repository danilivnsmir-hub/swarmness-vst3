#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

/**
 * Optional artwork (Source/Assets/Skin, see ASSET_SPEC.md and README.md there).
 *
 * Every PNG in that folder is built in; Skin::has ("panel") tells whether "panel.png" is there, and
 * every element that has artwork draws it and falls back to its drawn-in-code look otherwise, so any
 * part of the set can be missing.
 *
 * The artwork is drawn at 2x: one image pixel is half a UI unit. Images are kept as a small chain of
 * pre-shrunk copies and the one closest to the screen's pixel density is used, so they stay crisp at
 * 100% as well as at 200%. Stretchable pieces (panels, buttons) are put together at the exact pixel
 * size on screen once and cached, so their slices never show seams.
 *
 * Message thread only.
 */
namespace Skin
{
    /** UI units per image pixel: the artwork is drawn at 2x. */
    constexpr float unitsPerPixel = 0.5f;

    namespace detail
    {
        /** Half-size copy (2x2 box filter on premultiplied pixels). */
        inline juce::Image halve (const juce::Image& src)
        {
            const int w = juce::jmax (1, src.getWidth() / 2), h = juce::jmax (1, src.getHeight() / 2);
            const auto in = src.convertedToFormat (juce::Image::ARGB);
            juce::Image out (juce::Image::ARGB, w, h, false);
            const juce::Image::BitmapData s (in, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData d (out, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const int x0 = juce::jmin (2 * x, s.width - 1), x1 = juce::jmin (2 * x + 1, s.width - 1);
                    const int y0 = juce::jmin (2 * y, s.height - 1), y1 = juce::jmin (2 * y + 1, s.height - 1);
                    const juce::uint8* p[4] { s.getPixelPointer (x0, y0), s.getPixelPointer (x1, y0),
                                              s.getPixelPointer (x0, y1), s.getPixelPointer (x1, y1) };
                    auto* o = d.getPixelPointer (x, y);
                    for (int c = 0; c < 4; ++c)
                        o[c] = (juce::uint8) ((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
                }
            return out;
        }

        struct Art
        {
            std::vector<juce::Image> levels;   // [0] = as drawn (2x), then 1/2, 1/4, ...

            /** The copy to draw when one full-size image pixel covers `devicePx` screen pixels,
                and how many screen pixels one pixel of that copy then covers. */
            std::pair<const juce::Image*, float> pick (float devicePx)
            {
                size_t level = 0;
                float s = devicePx;
                // step down while the next copy still has at least as many pixels as the screen
                while (s * 2.0f <= 1.05f && levels.back().getWidth() > 8 && levels.back().getHeight() > 8)
                {
                    if (level + 1 >= levels.size())
                        levels.push_back (halve (levels.back()));
                    ++level;
                    s *= 2.0f;
                }
                return { &levels[level], s };
            }
        };

        struct Cache : juce::DeletedAtShutdown
        {
            std::map<juce::String, Art> art;
            std::map<juce::String, juce::Image> composed;   // sliced pieces at their on-screen pixel size

            ~Cache() override { instance() = nullptr; }
            static Cache*& instance() noexcept { static Cache* p = nullptr; return p; }
            static Cache& get()
            {
                auto& p = instance();
                if (p == nullptr) p = new Cache();
                return *p;
            }

            Art* find (const juce::String& name)
            {
                auto it = art.find (name);
                if (it == art.end())
                {
                    Art a;
                    int size = 0;
                    if (const auto* data = BinaryData::getNamedResource ((name + "_png").toRawUTF8(), size))
                        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
                            a.levels.push_back (img.convertedToFormat (juce::Image::ARGB));
                    it = art.emplace (name, std::move (a)).first;
                }
                return it->second.levels.empty() ? nullptr : &it->second;
            }
        };

        inline float deviceScale (juce::Graphics& g)
        {
            return juce::jmax (0.05f, g.getInternalContext().getPhysicalPixelScaleFactor());
        }

        /** Draws `img` stretched over `dest` with the best resampling. */
        inline void blit (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> dest, float alpha)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.setOpacity (alpha);
            g.drawImageTransformed (img, juce::AffineTransform::scale (dest.getWidth() / (float) img.getWidth(), dest.getHeight() / (float) img.getHeight())
                                             .translated (dest.getX(), dest.getY()));
        }
    }

    /** The image "name.png" as built in (at 2x), invalid when there is none. */
    inline juce::Image get (const juce::String& name)
    {
        if (auto* a = detail::Cache::get().find (name))
            return a->levels.front();
        return {};
    }

    inline bool has (const juce::String& name) { return detail::Cache::get().find (name) != nullptr; }

    /** The artwork's size in UI units (half its pixel size), empty when there is none. */
    inline juce::Rectangle<float> naturalSize (const juce::String& name)
    {
        if (auto* a = detail::Cache::get().find (name))
            return { (float) a->levels.front().getWidth() * unitsPerPixel, (float) a->levels.front().getHeight() * unitsPerPixel };
        return {};
    }

    /** Stretches the artwork over `dest`. Returns false (drawing nothing) when there is none. */
    inline bool draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> dest, float alpha = 1.0f)
    {
        auto* a = detail::Cache::get().find (name);
        if (a == nullptr || dest.isEmpty())
            return false;
        const auto& full = a->levels.front();
        const float devicePx = dest.getWidth() * detail::deviceScale (g) / (float) full.getWidth();
        detail::blit (g, *a->pick (devicePx).first, dest, alpha);
        return true;
    }

    /** Draws the artwork inside `area` keeping its aspect (`placement` as for drawImage). */
    inline bool drawFitted (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area,
                            juce::RectanglePlacement placement = juce::RectanglePlacement::centred, float alpha = 1.0f)
    {
        const auto natural = naturalSize (name);
        if (natural.isEmpty())
            return false;
        return draw (g, name, placement.appliedTo (natural, area), alpha);
    }

    /** Draws the artwork rotated by `angle` (radians, clockwise) about the centre of `dest`. */
    inline bool drawRotated (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> dest, float angle, float alpha = 1.0f)
    {
        auto* a = detail::Cache::get().find (name);
        if (a == nullptr || dest.isEmpty())
            return false;
        const auto& full = a->levels.front();
        const juce::Image* const img = a->pick (dest.getWidth() * detail::deviceScale (g) / (float) full.getWidth()).first;
        juce::Graphics::ScopedSaveState s (g);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (alpha);
        g.drawImageTransformed (*img, juce::AffineTransform::translation (-(float) img->getWidth() * 0.5f, -(float) img->getHeight() * 0.5f)
                                          .scaled (dest.getWidth() / (float) img->getWidth(), dest.getHeight() / (float) img->getHeight())
                                          .rotated (angle)
                                          .translated (dest.getCentre()));
        return true;
    }

    namespace detail
    {
        /** A sliced piece put together at W x H screen pixels. `border`: the fixed edges in image pixels
            (0 = that direction stretches whole); `scale` = screen pixels per image pixel for the edges. */
        inline juce::Image compose (Art& a, int W, int H, juce::BorderSize<float> border, float scale)
        {
            const auto picked = a.pick (scale);
            const juce::Image* const src = picked.first;
            const float s = picked.second;   // screen pixels per pixel of that copy
            const float k = (float) src->getWidth() / (float) a.levels.front().getWidth();   // level pixels per full pixel
            const int sw = src->getWidth(), sh = src->getHeight();
            // edges in the source copy (never more than the whole) and on screen (shrunk to fit if need be)
            auto edge = [k] (float px, int size) { return juce::jlimit (0, size, juce::roundToInt (px * k)); };
            const int sl = edge (border.getLeft(), sw), sr = juce::jmin (sw - sl, edge (border.getRight(), sw));
            const int st = edge (border.getTop(), sh),  sb = juce::jmin (sh - st, edge (border.getBottom(), sh));
            auto fit = [s] (int a1, int a2, int size, int& d1, int& d2)
            {
                d1 = juce::roundToInt ((float) a1 * s);
                d2 = juce::roundToInt ((float) a2 * s);
                if (d1 + d2 > size && d1 + d2 > 0)
                {
                    const float f = (float) size / (float) (d1 + d2);
                    d1 = juce::roundToInt ((float) d1 * f);
                    d2 = size - d1;
                }
            };
            int dl, dr, dt, db;
            fit (sl, sr, W, dl, dr);
            fit (st, sb, H, dt, db);

            juce::Image out (juce::Image::ARGB, W, H, true);
            juce::Graphics g (out);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            const int srcX[4] { 0, sl, sw - sr, sw }, srcY[4] { 0, st, sh - sb, sh };
            const int dstX[4] { 0, dl, W - dr, W },    dstY[4] { 0, dt, H - db, H };
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i)
                {
                    const juce::Rectangle<int> from (srcX[i], srcY[j], srcX[i + 1] - srcX[i], srcY[j + 1] - srcY[j]);
                    const juce::Rectangle<int> to (dstX[i], dstY[j], dstX[i + 1] - dstX[i], dstY[j + 1] - dstY[j]);
                    if (from.isEmpty() || to.isEmpty())
                        continue;
                    const auto piece = src->getClippedImage (from);
                    g.drawImageTransformed (piece, juce::AffineTransform::scale ((float) to.getWidth() / (float) from.getWidth(),
                                                                                 (float) to.getHeight() / (float) from.getHeight())
                                                       .translated ((float) to.getX(), (float) to.getY()));
                }
            return out;
        }
    }

    /** Draws the artwork over `dest` with its `border` (image pixels; BorderSize order: top, left, bottom, right) kept at
        `borderScale` UI units per image pixel (0.5 = as drawn) and the rest stretched. The piece is put
        together at its exact pixel size on screen and cached, so the slices never show seams. */
    inline bool drawSliced (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> dest,
                            juce::BorderSize<float> border, float borderScale = unitsPerPixel, float alpha = 1.0f)
    {
        auto& cache = detail::Cache::get();
        auto* a = cache.find (name);
        if (a == nullptr || dest.isEmpty())
            return false;
        const float ds = detail::deviceScale (g);
        const int W = juce::jmax (1, juce::roundToInt (dest.getWidth() * ds)), H = juce::jmax (1, juce::roundToInt (dest.getHeight() * ds));
        const float scale = borderScale * ds;
        const auto key = name + "|" + juce::String (W) + "x" + juce::String (H) + "|" + juce::String (border.getLeft()) + ","
                       + juce::String (border.getTop()) + "," + juce::String (border.getRight()) + "," + juce::String (border.getBottom())
                       + "|" + juce::String (scale, 3);
        auto it = cache.composed.find (key);
        if (it == cache.composed.end())
        {
            if (cache.composed.size() > 400)
                cache.composed.clear();
            it = cache.composed.emplace (key, detail::compose (*a, W, H, border, scale)).first;
        }
        detail::blit (g, it->second, dest, alpha);
        return true;
    }

    /** 9-slice: the `corner` image pixels at each edge stay fixed (scaled by `cornerScale`, 1 = as drawn),
        the middle stretches. */
    inline bool drawNine (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> dest, float corner,
                          float cornerScale = 1.0f, float alpha = 1.0f)
    {
        return drawSliced (g, name, dest, { corner, corner, corner, corner }, unitsPerPixel * cornerScale, alpha);
    }

    /** 3-slice across: the artwork is scaled to the height of `dest`, its `cap` image pixels at the left and
        right ends keep their shape and the middle stretches. */
    inline bool drawThree (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> dest, float cap, float alpha = 1.0f)
    {
        const auto natural = naturalSize (name);
        if (natural.isEmpty())
            return false;
        return drawSliced (g, name, dest, { 0.0f, cap, 0.0f, cap }, unitsPerPixel * dest.getHeight() / natural.getHeight(), alpha);
    }
}
