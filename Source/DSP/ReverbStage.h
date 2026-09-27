#pragma once

#include "DSPUtils.h"
#include "Equalisers.h"
#include <array>
#include <vector>

/**
 * CRYPT: stereo reverb.
 *
 *  - Algorithmic: pre-delay -> early reflections (a tapped delay per type, different left /
 *    right) -> 4-stage all-pass input diffuser per channel -> 16-line feedback delay network:
 *    orthonormal Hadamard mixing, an all-pass inside every line (echo density builds up fast,
 *    no flutter), two-band decay per line (lows and highs each reach their own RT60 - TONE sets
 *    how much faster the highs die), slow modulated reads against metallic ringing, and
 *    decorrelated left / right output taps. ROOM / PLATE / HALL / ABYSS set the line lengths,
 *    early reflections, diffusion, modulation, bass decay and colour.
 *  - IR: a loaded impulse response through juce::dsp::Convolution (zero latency). The IR is
 *    handed over from the message thread through a try-locked slot, so the audio thread
 *    never blocks and never allocates.
 *
 * Both share pre-delay, TONE, LOW CUT, DUCK (the wet dips while you play) and MIX.
 * Switching the block off stops feeding it but lets the tail ring out (spill-over).
 */
class ReverbStage
{
public:
    enum Type : int { room = 0, plate, hall, abyss, impulse };

    struct Settings
    {
        bool on = false;
        int type = hall;
        float mix = 0.25f, decay = 2.5f, size = 0.6f, preDelayMs = 15.0f, tone = 0.5f;
        float lowCutHz = 150.0f, mod = 0.3f, duck = 0.0f;
    };

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        maxBlockSize = juce::jmax (1, maxBlock);

        preDelay.prepare ((int) std::ceil (0.26 * sr) + 4);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < kDiffusers; ++s)
                diffusers[(size_t) ch][(size_t) s].prepare ((int) std::ceil (0.03 * sr) + 4);

        // longest line: 72 ms x 1.45 (ABYSS) x 1.45 (SIZE) + modulation
        const int maxLine = (int) std::ceil ((0.0722 * 1.45 * 1.45 + 0.006) * sr) + 8;
        for (auto& line : lines)
            line.prepare (maxLine);
        for (auto& ap : loopAllPass)
            ap.prepare ((int) std::ceil (0.0065 * sr) + 4);
        for (auto& er : erLine)
            er.prepare ((int) std::ceil (0.16 * 1.25 * sr) + 4);

        wet.setSize (2, maxBlockSize, false, false, true);
        dryCopy.setSize (2, maxBlockSize, false, false, true);

        convolution.reset();
        convolution.prepare ({ sr, (juce::uint32) maxBlockSize, 2 });

        feedGain.reset (sr, 0.03);
        dryGain.reset (sr, 0.03);
        wetGain.reset (sr, 0.03);
        typeFade.reset (sr, 0.012);
        typeFade.setCurrentAndTargetValue (1.0f);

        sizeSmooth.setTime (sr, 0.35);
        preDelaySmooth.setTime (sr, 0.08);
        duckEnv = 0.0f;
        duckGain.setTime (sr, 0.03);
        duckGain.reset (1.0f);
        duckAttack  = (float) (1.0 - std::exp (-1.0 / (0.005 * sr)));
        duckRelease = (float) (1.0 - std::exp (-1.0 / (0.30 * sr)));

        activeType = settings.type;
        applyTypeInstantly();
        feedGain.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        dryGain.setCurrentAndTargetValue (targetDry());
        wetGain.setCurrentAndTargetValue (targetWet());
        reset();
    }

    void reset() noexcept
    {
        preDelay.clear();
        for (auto& ch : diffusers)
            for (auto& d : ch)
                d.clear();
        for (auto& line : lines)
            line.clear();
        for (auto& ap : loopAllPass)
            ap.clear();
        for (auto& er : erLine)
            er.clear();
        for (auto& d : damp)
            d = 0.0f;
        for (auto& f : outFilters)
            for (auto& s : f)
                s.reset();
        silentSamples = 0;
        idle = true;
        for (int j = 0; j < kLines; ++j)
            lfo[(size_t) j] = { std::cos (1.7f * (float) j), std::sin (1.7f * (float) j) };
    }

    /** Early reflections of the current type (ms, gain; left / right), for the editor's tail view too. */
    struct Reflection { float ms, gain; };

    void setParams (const Settings& s) noexcept
    {
        settings = s;
        settings.mix = juce::jlimit (0.0f, 1.0f, s.mix);
        feedGain.setTargetValue (s.on ? 1.0f : 0.0f);
        dryGain.setTargetValue (targetDry());
        wetGain.setTargetValue (targetWet());
        if (s.type != activeType && ! typeFade.isSmoothing())
            typeFade.setTargetValue (0.0f);   // fade the tail out, swap the room, fade back in
    }

    /** Message thread: hand an impulse response to the audio thread (ownership is taken). */
    void setImpulseResponse (juce::AudioBuffer<float>&& ir, double irSampleRate)
    {
        const juce::SpinLock::ScopedLockType sl (irLock);
        pendingIR = std::move (ir);
        pendingIRRate = irSampleRate;
        irPending = true;
    }

    /** Message thread: forget the impulse response (IR mode goes silent). */
    void clearImpulseResponse()
    {
        const juce::SpinLock::ScopedLockType sl (irLock);
        pendingIR.setSize (0, 0);
        irPending = false;
        irCleared = true;
    }

    bool isIdle() const noexcept { return idle; }

    /** Peak of the wet signal in the last block (for the editor). */
    float getWetLevel() const noexcept { return wetLevel.load (std::memory_order_relaxed); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = juce::jmin (numChannels, 2);
        pickUpImpulseResponse();

        const bool feeding = settings.on || feedGain.isSmoothing();
        float inPeak = 0.0f;
        if (feeding)
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    inPeak = juce::jmax (inPeak, std::abs (audio[ch][i]));

        // Idle: nothing coming in and the tail has died away.
        if (idle && (! feeding || inPeak < 1.0e-6f) && ! dryGain.isSmoothing() && ! wetGain.isSmoothing())
        {
            if (settings.type != activeType)
            {
                activeType = settings.type;
                applyTypeInstantly();
                typeFade.setCurrentAndTargetValue (1.0f);
            }
            applyDryGainOnly (audio, numChannels, numSamples);
            wetLevel.store (0.0f, std::memory_order_relaxed);
            return;
        }
        idle = false;

        // Keep the dry signal (the block works in place)
        for (int ch = 0; ch < numChannels; ++ch)
            dryCopy.copyFrom (ch, 0, audio[ch], numSamples);
        if (numChannels == 1)
            dryCopy.copyFrom (1, 0, audio[0], numSamples);

        updateBlockParameters();
        float* wl = wet.getWritePointer (0);
        float* wr = wet.getWritePointer (1);
        const float* dl = dryCopy.getReadPointer (0);
        const float* dr = dryCopy.getReadPointer (1);

        // ---- pre-delay (and the feed on/off fade)
        for (int i = 0; i < numSamples; ++i)
        {
            const float f = feedGain.getNextValue();
            const float pd = preDelaySmooth.process (settings.preDelayMs * 0.001f * (float) sampleRate);
            preDelay.push (dl[i] * f, dr[i] * f);
            preDelay.read (pd, wl[i], wr[i]);
        }

        if (activeType == impulse)
            processConvolution (numSamples);
        else
            processFdn (wl, wr, numSamples);

        // ---- wet colour: low cut + tone high cut, then ducking, fades and the mix
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            // DUCK: follow the dry input, dip the wet while you play
            const float in = juce::jmax (std::abs (dl[i]), std::abs (dr[i]));
            duckEnv += (in > duckEnv ? duckAttack : duckRelease) * (in - duckEnv);
            float duckTarget = 1.0f;
            if (settings.duck > 0.0f)
            {
                const float over = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (duckEnv, -100.0f) + 48.0f) / 30.0f);
                duckTarget = juce::Decibels::decibelsToGain (-24.0f * settings.duck * over);
            }
            const float dg = duckGain.process (duckTarget);
            const float tf = typeFade.getNextValue();
            const float wg = wetGain.getNextValue() * dg * tf;
            const float drg = dryGain.getNextValue();

            float l = wl[i], r = wr[i];
            for (int f = 0; f < 2; ++f)
            {
                l = outFilters[0][(size_t) f].process (outCoeffs[(size_t) f], l);
                r = outFilters[1][(size_t) f].process (outCoeffs[(size_t) f], r);
            }
            peak = juce::jmax (peak, std::abs (l), std::abs (r));

            if (numChannels == 2)
            {
                audio[0][i] = dl[i] * drg + l * wg;
                audio[1][i] = dr[i] * drg + r * wg;
            }
            else
            {
                audio[0][i] = dl[i] * drg + 0.5f * (l + r) * wg;
            }
        }
        wetLevel.store (peak * wetGain.getCurrentValue(), std::memory_order_relaxed);

        // Swap the room type once the old one has faded out
        if (typeFade.getCurrentValue() <= 0.0f && ! typeFade.isSmoothing())
        {
            activeType = settings.type;
            clearTail();
            applyTypeInstantly();
            typeFade.setTargetValue (1.0f);
        }

        // Tail detection: go idle once the wet has been silent for a while and nothing feeds it
        if (peak < 3.0e-6f && (inPeak < 1.0e-6f || ! settings.on) && ! feedGain.isSmoothing())
        {
            silentSamples += numSamples;
            if (silentSamples > (int) (0.25 * sampleRate))
            {
                clearTail();
                idle = true;
            }
        }
        else
        {
            silentSamples = 0;
        }
    }

private:
    //==============================================================================
    static constexpr int kLines = 16, kDiffusers = 4, kReflections = 10;

    /** Power-of-two circular buffer with fractional (Hermite) reads. */
    struct Line
    {
        std::vector<float> data;
        int mask = 0, writePos = 0;

        void prepare (int maxDelay)
        {
            int size = 1;
            while (size < maxDelay + 4) size <<= 1;
            data.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writePos = 0;
        }
        void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); }
        void push (float x) noexcept { data[(size_t) writePos] = x; writePos = (writePos + 1) & mask; }
        float at (int delay) const noexcept { return data[(size_t) ((writePos - delay) & mask)]; }
        float read (float delay) const noexcept
        {
            const int d = (int) delay;
            const float frac = delay - (float) d;
            return swarm::hermite (at (d - 1), at (d), at (d + 1), at (d + 2), frac);
        }
    };

    /** Stereo pre-delay with linear interpolation (smoothly changeable). */
    struct PreDelay
    {
        std::array<Line, 2> ch;
        void prepare (int maxDelay) { for (auto& c : ch) c.prepare (maxDelay); }
        void clear() noexcept { for (auto& c : ch) c.clear(); }
        void push (float l, float r) noexcept { ch[0].push (l); ch[1].push (r); }
        void read (float delay, float& l, float& r) const noexcept
        {
            delay = juce::jmax (1.0f, delay);
            const int d = (int) delay;
            const float frac = delay - (float) d;
            l = ch[0].at (d) + frac * (ch[0].at (d + 1) - ch[0].at (d));
            r = ch[1].at (d) + frac * (ch[1].at (d + 1) - ch[1].at (d));
        }
    };

    /** Schroeder all-pass diffuser. */
    struct AllPass
    {
        Line line;
        int delay = 100;
        void prepare (int maxDelay) { line.prepare (maxDelay); }
        void clear() noexcept { line.clear(); }
        float process (float x, float g) noexcept
        {
            const float d = line.at (delay);
            const float v = x + g * d;
            line.push (v);
            return d - g * v;
        }
    };

    // scale = line lengths, diffusion = input all-passes, early = reflection level, modScale,
    // toneShift = brighter / darker, bass = how much longer the lows ring than the mids
    struct TypeConfig { float scale, diffusion, early, modScale, toneShift, bass; };

    static TypeConfig configFor (int type) noexcept
    {
        switch (type)
        {
            case room:  return { 0.42f, 0.62f, 0.55f, 0.4f, 0.0f,   1.0f  };
            case plate: return { 0.62f, 0.78f, 0.00f, 0.8f, 0.18f,  0.85f };
            case abyss: return { 1.45f, 0.72f, 0.25f, 1.9f, -0.12f, 1.35f };
            case hall:
            default:    return { 1.00f, 0.70f, 0.35f, 1.0f, 0.0f,   1.15f };
        }
    }

    /** Early reflections per type: a few discrete echoes before the dense tail, different on each side. */
    static const std::array<std::array<Reflection, kReflections>, 2>& reflectionsFor (int type) noexcept
    {
        static const std::array<std::array<Reflection, kReflections>, 2> roomER {{
            {{ { 3.1f, 0.80f }, { 5.9f, 0.62f }, { 8.3f, 0.55f }, { 11.9f, 0.47f }, { 14.1f, 0.40f }, { 17.9f, 0.33f }, { 21.1f, 0.28f }, { 25.7f, 0.22f }, { 29.3f, 0.18f }, { 34.1f, 0.14f } }},
            {{ { 4.3f, 0.78f }, { 6.7f, 0.60f }, { 9.7f, 0.52f }, { 12.7f, 0.45f }, { 15.9f, 0.38f }, { 19.3f, 0.32f }, { 23.3f, 0.26f }, { 27.1f, 0.21f }, { 31.3f, 0.17f }, { 36.7f, 0.13f } }} }};
        static const std::array<std::array<Reflection, kReflections>, 2> hallER {{
            {{ { 9.7f, 0.62f }, { 15.1f, 0.55f }, { 21.7f, 0.50f }, { 27.9f, 0.44f }, { 36.1f, 0.38f }, { 43.3f, 0.33f }, { 52.9f, 0.28f }, { 61.1f, 0.23f }, { 71.9f, 0.19f }, { 83.3f, 0.15f } }},
            {{ { 11.3f, 0.60f }, { 17.3f, 0.54f }, { 23.9f, 0.48f }, { 31.1f, 0.43f }, { 38.9f, 0.37f }, { 47.1f, 0.32f }, { 55.7f, 0.27f }, { 65.3f, 0.22f }, { 75.1f, 0.18f }, { 87.7f, 0.14f } }} }};
        static const std::array<std::array<Reflection, kReflections>, 2> abyssER {{
            {{ { 21.1f, 0.55f }, { 33.7f, 0.50f }, { 47.3f, 0.45f }, { 59.9f, 0.40f }, { 71.3f, 0.36f }, { 86.9f, 0.32f }, { 99.1f, 0.28f }, { 113.3f, 0.24f }, { 127.9f, 0.20f }, { 143.1f, 0.17f } }},
            {{ { 25.3f, 0.53f }, { 38.9f, 0.48f }, { 51.7f, 0.44f }, { 64.3f, 0.39f }, { 77.9f, 0.35f }, { 91.3f, 0.31f }, { 105.7f, 0.27f }, { 119.9f, 0.23f }, { 133.1f, 0.19f }, { 149.3f, 0.16f } }} }};
        static const std::array<std::array<Reflection, kReflections>, 2> none {};
        switch (type)
        {
            case room:  return roomER;
            case hall:  return hallER;
            case abyss: return abyssER;
            default:    return none;
        }
    }

    float targetDry() const noexcept { return settings.on ? juce::jmin (1.0f, 2.0f * (1.0f - settings.mix)) : 1.0f; }
    float targetWet() const noexcept { return juce::jmin (1.0f, 2.0f * settings.mix); }

    void applyDryGainOnly (float* const* audio, int numChannels, int numSamples) noexcept
    {
        // Idle reverb: only the dry level matters (and it is 1 unless the block is on at MIX > 50%).
        const float g = dryGain.getCurrentValue();
        if (std::abs (g - 1.0f) > 1.0e-6f)
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply (audio[ch], g, numSamples);
        feedGain.skip (numSamples);
        wetGain.skip (numSamples);
    }

    void applyTypeInstantly() noexcept
    {
        cfg = configFor (activeType == impulse ? hall : activeType);
        // Diffuser lengths (ms), slightly different per side for width
        static constexpr float diffMs[2][kDiffusers] { { 4.77f, 3.59f, 12.73f, 9.30f }, { 4.99f, 3.73f, 13.19f, 8.91f } };
        const float dscale = std::sqrt (cfg.scale);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < kDiffusers; ++s)
                diffusers[(size_t) ch][(size_t) s].delay = juce::jmax (1, (int) (diffMs[ch][s] * dscale * 0.001f * (float) sampleRate));
        sizeSmooth.reset (sizeScale());
        preDelaySmooth.reset (settings.preDelayMs * 0.001f * (float) sampleRate);
    }

    float sizeScale() const noexcept { return cfg.scale * (0.55f + 0.9f * juce::jlimit (0.0f, 1.0f, settings.size)); }

    void clearTail() noexcept
    {
        for (auto& ch : diffusers)
            for (auto& d : ch)
                d.clear();
        for (auto& line : lines)
            line.clear();
        for (auto& ap : loopAllPass)
            ap.clear();
        for (auto& er : erLine)
            er.clear();
        for (auto& d : damp)
            d = 0.0f;
    }

    void updateBlockParameters() noexcept
    {
        // Output colour: low cut (2nd order) and a gentle high cut that follows TONE
        const float tone = juce::jlimit (0.0f, 1.0f, settings.tone + cfg.toneShift);
        outCoeffs[0] = swarm::EqBand::make (swarm::EqBand::Type::highPass, sampleRate, settings.lowCutHz, 0.7071f, 0.0f);
        outCoeffs[1] = swarm::EqBand::make (swarm::EqBand::Type::lowPass, sampleRate, 2500.0f * std::exp2 (tone * 3.0f), 0.6f, 0.0f);

        // Two-band decay inside the loop: DECAY is the RT60 of the mids; the lows ring a little longer
        // (per type) and the highs die faster the darker TONE is
        const float crossHz = 1800.0f * std::exp2 (tone * 1.2f);
        dampCoeff = 1.0f - std::exp (-swarm::kTwoPi * juce::jmin (crossHz, (float) sampleRate * 0.45f) / (float) sampleRate);
        highRatio = 0.18f + 0.72f * tone;
        modDepth = settings.mod * cfg.modScale * 0.0011f * (float) sampleRate;
        rt60Samples = juce::jmax (0.05f, settings.decay) * (float) sampleRate;

        // LFO increments (slow, unrelated rates)
        static constexpr float rates[kLines] { 0.13f, 0.21f, 0.34f, 0.47f, 0.59f, 0.71f, 0.83f, 0.97f,
                                               0.17f, 0.27f, 0.39f, 0.52f, 0.63f, 0.77f, 0.89f, 1.03f };
        for (int j = 0; j < kLines; ++j)
        {
            const float w = swarm::kTwoPi * rates[j] * (0.6f + 0.8f * settings.mod) / (float) sampleRate;
            lfoRot[(size_t) j] = { std::cos (w), std::sin (w) };
        }
    }

    void processFdn (float* wl, float* wr, int numSamples) noexcept
    {
        static constexpr float baseMs[kLines] { 17.3f, 19.9f, 22.7f, 25.1f, 28.3f, 31.1f, 34.7f, 37.9f,
                                                41.3f, 45.1f, 48.7f, 53.3f, 57.1f, 61.9f, 66.7f, 72.1f };
        static constexpr float allPassMs[kLines] { 1.13f, 2.71f, 1.61f, 3.37f, 2.03f, 1.39f, 3.89f, 2.39f,
                                                   1.79f, 3.13f, 1.27f, 2.83f, 3.61f, 1.97f, 2.57f, 1.51f };
        // output taps: two different Hadamard rows -> decorrelated left / right
        static constexpr float outSignL[kLines] { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 };
        static constexpr float outSignR[kLines] { 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1 };
        const float g = cfg.diffusion;
        const float msToSamples = 0.001f * (float) sampleRate;
        const float targetScale = sizeScale();
        const auto& er = reflectionsFor (activeType);
        const bool hasEarly = cfg.early > 0.0f;
        const float erScale = std::sqrt (sizeSmooth.get() / juce::jmax (0.01f, cfg.scale)) * msToSamples;

        std::array<float, kLines> lengths {}, gLow {}, gHigh {};
        std::array<int, kLines> apDelay {};
        for (int i = 0; i < numSamples; ++i)
        {
            // line lengths glide with SIZE; decay gains per line and band from RT60
            const float scale = sizeSmooth.process (targetScale);
            if ((i & 15) == 0)
                for (int j = 0; j < kLines; ++j)
                {
                    lengths[(size_t) j] = baseMs[j] * scale * msToSamples;
                    apDelay[(size_t) j] = juce::jmax (1, (int) (allPassMs[j] * std::sqrt (scale) * msToSamples));
                    const float loop = lengths[(size_t) j] + (float) apDelay[(size_t) j];
                    gLow[(size_t) j]  = std::pow (10.0f, -3.0f * loop / (rt60Samples * cfg.bass));
                    gHigh[(size_t) j] = std::pow (10.0f, -3.0f * loop / (rt60Samples * highRatio));
                }

            // early reflections: discrete echoes, different on each side
            float l = wl[i], r = wr[i];
            float erL = 0.0f, erR = 0.0f;
            if (hasEarly)
            {
                erLine[0].push (l);
                erLine[1].push (r);
                for (int k = 0; k < kReflections; ++k)
                {
                    erL += er[0][(size_t) k].gain * erLine[(k & 1) == 0 ? 0 : 1].at ((int) (er[0][(size_t) k].ms * erScale));
                    erR += er[1][(size_t) k].gain * erLine[(k & 1) == 0 ? 1 : 0].at ((int) (er[1][(size_t) k].ms * erScale));
                }
                erL *= 0.45f;
                erR *= 0.45f;
            }

            // input diffusion (the reflections feed the tail, so it grows out of them)
            l += 0.5f * erL;
            r += 0.5f * erR;
            for (int s = 0; s < kDiffusers; ++s)
            {
                l = diffusers[0][(size_t) s].process (l, g);
                r = diffusers[1][(size_t) s].process (r, g);
            }

            // read the lines (modulated), two-band decay
            std::array<float, kLines> x {};
            float outL = 0.0f, outR = 0.0f;
            for (int j = 0; j < kLines; ++j)
            {
                auto& ph = lfo[(size_t) j];
                const auto& rot = lfoRot[(size_t) j];
                ph = { ph.first * rot.first - ph.second * rot.second, ph.first * rot.second + ph.second * rot.first };
                const float delay = juce::jmax (4.0f, lengths[(size_t) j] + modDepth * (1.0f + ph.second));
                const float y = lines[(size_t) j].read (delay);
                outL += outSignL[j] * y;
                outR += outSignR[j] * y;
                auto& lp = damp[(size_t) j];
                lp += dampCoeff * (y - lp);
                x[(size_t) j] = gLow[(size_t) j] * lp + gHigh[(size_t) j] * (y - lp);
            }

            hadamard16 (x);

            // an all-pass in every line (echo density), then inject the diffused input:
            // left into even lines, right into odd lines, with alternating signs
            for (int j = 0; j < kLines; ++j)
            {
                auto& ap = loopAllPass[(size_t) j];
                ap.delay = apDelay[(size_t) j];
                const float v = ap.process (x[(size_t) j], 0.5f);
                lines[(size_t) j].push (v + ((j & 1) == 0 ? l : r) * ((j & 2) != 0 ? -kInject : kInject));
            }

            wl[i] = kOutScale * outL + cfg.early * erL;
            wr[i] = kOutScale * outR + cfg.early * erR;
        }

        // keep the LFO phasors on the unit circle
        for (auto& ph : lfo)
        {
            const float m = 1.0f / std::sqrt (ph.first * ph.first + ph.second * ph.second);
            ph.first *= m;
            ph.second *= m;
        }
    }

    static void hadamard16 (std::array<float, kLines>& x) noexcept
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                    x[(size_t) j] = a + b;
                    x[(size_t) (j + h)] = a - b;
                }
        for (auto& v : x)
            v *= 0.25f;   // 1 / sqrt (16): orthonormal
    }

    void processConvolution (int numSamples) noexcept
    {
        if (! irLoaded)
        {
            wet.clear (0, numSamples);
            return;
        }
        juce::dsp::AudioBlock<float> block (wet.getArrayOfWritePointers(), 2, (size_t) numSamples);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        // Self-healing: a convolution that ever produces garbage is reset instead of hissing forever
        bool bad = false;
        for (int ch = 0; ch < 2 && ! bad; ++ch)
        {
            const float* w = wet.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! (std::abs (w[i]) < 32.0f)) { bad = true; break; }
        }
        if (bad)
        {
            convolution.reset();
            wet.clear (0, numSamples);
            for (auto& ch : outFilters)
                for (auto& f : ch)
                    f = {};
        }
    }

    void pickUpImpulseResponse() noexcept
    {
        const juce::GenericScopedTryLock<juce::SpinLock> sl (irLock);
        if (! sl.isLocked())
            return;
        if (irPending)
        {
            // wait-free hand-over (the convolution prepares the IR on its own background thread)
            // (cleaned and levelled on the message thread: irtools::sanitise + a fixed energy)
            convolution.loadImpulseResponse (std::move (pendingIR), pendingIRRate, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            irPending = false;
            irLoaded = true;
        }
        if (irCleared)
        {
            irCleared = false;
            irLoaded = false;
        }
    }

    static constexpr float kOutScale = 0.3f;    // FDN wet level
    static constexpr float kInject   = 0.35f;   // input into each line
public:
    /** Loaded IRs are levelled to this energy (sqrt of the sum of squares over both channels). */
    static constexpr float kIrLevel  = 0.87f;
private:

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    Settings settings;
    int activeType = hall;
    TypeConfig cfg = configFor (hall);

    PreDelay preDelay;
    std::array<std::array<AllPass, kDiffusers>, 2> diffusers;
    std::array<Line, kLines> lines;
    std::array<AllPass, kLines> loopAllPass;
    std::array<Line, 2> erLine;
    std::array<float, kLines> damp {};
    std::array<std::pair<float, float>, kLines> lfo {}, lfoRot {};
    float dampCoeff = 0.5f, highRatio = 0.5f, modDepth = 0.0f, rt60Samples = 44100.0f;

    std::array<swarm::EqBand::Coeffs, 2> outCoeffs {};
    std::array<std::array<swarm::EqBand::State, 2>, 2> outFilters {};

    juce::AudioBuffer<float> wet, dryCopy;
    juce::SmoothedValue<float> feedGain, dryGain, wetGain, typeFade;
    swarm::OnePole sizeSmooth, preDelaySmooth, duckGain;
    float duckEnv = 0.0f, duckAttack = 0.01f, duckRelease = 0.001f;

    juce::dsp::Convolution convolution;
    juce::SpinLock irLock;
    juce::AudioBuffer<float> pendingIR;
    double pendingIRRate = 44100.0;
    bool irPending = false, irCleared = false, irLoaded = false;

    int silentSamples = 0;
    bool idle = true;
    std::atomic<float> wetLevel { 0.0f };
};
