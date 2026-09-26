#pragma once

#include "DSPUtils.h"
#include "Equalisers.h"
#include <array>
#include <vector>

/**
 * CRYPT: stereo reverb.
 *
 *  - Algorithmic: pre-delay -> 4-stage all-pass diffuser per channel -> 8-line feedback delay
 *    network (orthogonal Hadamard mixing, per-line decay gains from RT60, damping low-pass,
 *    slow modulated reads against metallic ringing). ROOM / PLATE / HALL / ABYSS set the
 *    line lengths, diffusion, early reflections, modulation and colour.
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

        // longest line: 67 ms x 1.45 (ABYSS) x 1.45 (SIZE) + modulation
        const int maxLine = (int) std::ceil ((0.067 * 1.45 * 1.45 + 0.006) * sr) + 8;
        for (auto& line : lines)
            line.prepare (maxLine);

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
    static constexpr int kLines = 8, kDiffusers = 4;

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

    struct TypeConfig { float scale, diffusion, early, modScale, toneShift; };

    static TypeConfig configFor (int type) noexcept
    {
        switch (type)
        {
            case room:  return { 0.42f, 0.62f, 0.45f, 0.5f, 0.0f };
            case plate: return { 0.60f, 0.76f, 0.00f, 0.8f, 0.18f };
            case abyss: return { 1.45f, 0.70f, 0.05f, 1.9f, -0.12f };
            case hall:
            default:    return { 1.00f, 0.68f, 0.12f, 1.0f, 0.0f };
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
        for (auto& d : damp)
            d = 0.0f;
    }

    void updateBlockParameters() noexcept
    {
        // Output colour: low cut (2nd order) and a gentle high cut that follows TONE
        const float tone = juce::jlimit (0.0f, 1.0f, settings.tone + cfg.toneShift);
        outCoeffs[0] = swarm::EqBand::make (swarm::EqBand::Type::highPass, sampleRate, settings.lowCutHz, 0.7071f, 0.0f);
        outCoeffs[1] = swarm::EqBand::make (swarm::EqBand::Type::lowPass, sampleRate, 2500.0f * std::exp2 (tone * 3.0f), 0.6f, 0.0f);

        // Damping inside the loop: darker TONE = the highs die faster than the lows
        const float dampHz = 1400.0f * std::exp2 (tone * 3.4f);
        dampCoeff = 1.0f - std::exp (-swarm::kTwoPi * juce::jmin (dampHz, (float) sampleRate * 0.45f) / (float) sampleRate);
        modDepth = settings.mod * cfg.modScale * 0.0011f * (float) sampleRate;
        // x1.2: the damping filters shorten the broadband decay by about that much
        rt60Samples = juce::jmax (0.05f, settings.decay) * 1.2f * (float) sampleRate;

        // LFO increments (slow, unrelated rates)
        static constexpr float rates[kLines] { 0.13f, 0.21f, 0.34f, 0.47f, 0.59f, 0.71f, 0.83f, 0.97f };
        for (int j = 0; j < kLines; ++j)
        {
            const float w = swarm::kTwoPi * rates[j] * (0.6f + 0.8f * settings.mod) / (float) sampleRate;
            lfoRot[(size_t) j] = { std::cos (w), std::sin (w) };
        }
    }

    void processFdn (float* wl, float* wr, int numSamples) noexcept
    {
        static constexpr float baseMs[kLines] { 23.3f, 28.9f, 33.7f, 38.1f, 43.9f, 51.7f, 58.3f, 66.1f };
        static constexpr float outSignL[kLines] { 1, 0, -1, 0, 1, 0, -1, 0 };
        static constexpr float outSignR[kLines] { 0, 1, 0, 1, 0, -1, 0, -1 };
        const float g = cfg.diffusion;
        const float msToSamples = 0.001f * (float) sampleRate;
        const float targetScale = sizeScale();

        std::array<float, kLines> lengths {}, fb {};
        for (int i = 0; i < numSamples; ++i)
        {
            // line lengths glide with SIZE; decay gains per line from RT60
            const float scale = sizeSmooth.process (targetScale);
            if ((i & 15) == 0)
                for (int j = 0; j < kLines; ++j)
                {
                    lengths[(size_t) j] = baseMs[j] * scale * msToSamples;
                    fb[(size_t) j] = std::pow (10.0f, -3.0f * lengths[(size_t) j] / rt60Samples);
                }

            // input diffusion
            float l = wl[i], r = wr[i];
            for (int s = 0; s < kDiffusers; ++s)
            {
                l = diffusers[0][(size_t) s].process (l, g);
                r = diffusers[1][(size_t) s].process (r, g);
            }

            // read the lines (modulated), damp and apply the decay gain
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
                damp[(size_t) j] += dampCoeff * (y - damp[(size_t) j]);
                x[(size_t) j] = damp[(size_t) j] * fb[(size_t) j];
            }

            hadamard8 (x);

            // inject the diffused input: left into even lines, right into odd lines
            for (int j = 0; j < kLines; ++j)
                lines[(size_t) j].push (x[(size_t) j] + ((j & 1) == 0 ? l : r) * ((j & 2) != 0 ? -0.5f : 0.5f));

            wl[i] = kOutScale * outL + cfg.early * l;
            wr[i] = kOutScale * outR + cfg.early * r;
        }

        // keep the LFO phasors on the unit circle
        for (auto& ph : lfo)
        {
            const float m = 1.0f / std::sqrt (ph.first * ph.first + ph.second * ph.second);
            ph.first *= m;
            ph.second *= m;
        }
    }

    static void hadamard8 (std::array<float, kLines>& x) noexcept
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
            v *= 0.35355339f;   // 1 / sqrt (8): orthonormal
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
        juce::FloatVectorOperations::multiply (wet.getWritePointer (0), kIrScale, numSamples);
        juce::FloatVectorOperations::multiply (wet.getWritePointer (1), kIrScale, numSamples);
    }

    void pickUpImpulseResponse() noexcept
    {
        const juce::GenericScopedTryLock<juce::SpinLock> sl (irLock);
        if (! sl.isLocked())
            return;
        if (irPending)
        {
            // wait-free hand-over (the convolution prepares the IR on its own background thread)
            convolution.loadImpulseResponse (std::move (pendingIR), pendingIRRate, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
            irPending = false;
            irLoaded = true;
        }
        if (irCleared)
        {
            irCleared = false;
            irLoaded = false;
        }
    }

    static constexpr float kOutScale = 0.55f;   // FDN wet level
    static constexpr float kIrScale  = 5.0f;    // juce normalises IR energy to 0.125 (-18 dB)

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    Settings settings;
    int activeType = hall;
    TypeConfig cfg = configFor (hall);

    PreDelay preDelay;
    std::array<std::array<AllPass, kDiffusers>, 2> diffusers;
    std::array<Line, kLines> lines;
    std::array<float, kLines> damp {};
    std::array<std::pair<float, float>, kLines> lfo {}, lfoRot {};
    float dampCoeff = 0.5f, modDepth = 0.0f, rt60Samples = 44100.0f;

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
