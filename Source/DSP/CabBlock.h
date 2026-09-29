#pragma once

#include <JuceHeader.h>

#include "IrTools.h"

#include <array>
#include <cmath>
#include <complex>
#include <vector>

/**
 * CAB - a guitar speaker cabinet: four modelled cabinets, or a loaded impulse response.
 *
 * The modelled cabinets are original: each is a magnitude response built from the physics of a
 * speaker in a box (the cone / box resonance, open-back cancellation, low-mid box colour, the
 * presence peak, cone breakup ripple and the steep top roll-off), turned into a minimum-phase
 * impulse response (like a real close-miked speaker). MIC moves the microphone from the dust cap
 * (bright, aggressive) to the edge of the cone (darker, smoother); DISTANCE from right on the grille
 * (proximity bass) out into the room (floor reflection, less bass, a little air).
 *
 * The IRs are built on the message thread and handed to juce::dsp::Convolution (zero latency);
 * both engines (model / user IR) cross-fade when switching. A loaded IR is kept as it came from the
 * file and resampled to the host rate with a proper sinc (Convolution's own resampler dulls the top
 * octave), stereo IRs play in stereo, and an IR only takes over once its engine is really running
 * (before that, an offline bounce would hear the amp without a cabinet).
 */
class CabBlock
{
public:
    enum Type : int { open1x12 = 0, open2x12, brit4x12, modern4x12, ir, numTypes };

    struct Settings
    {
        bool on = false;
        int type = modern4x12;
        float mic = 0.3f;        // 0 = cap .. 1 = edge
        float distance = 0.2f;   // 0 = on the grille .. 1 = room
        float lowCutHz = 60.0f, highCutHz = 12000.0f;
        float levelDb = 0.0f;
    };

    //==============================================================================
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };
        {
            // the loaded files, resampled again for this rate
            const juce::ScopedLock sl (buildLock);
            for (size_t k = 0; k < 2; ++k)
                userHost[k] = userSource[k].getNumSamples() > 0 ? buildUserIR (userSource[k], userSourceRate[k], fs) : juce::AudioBuffer<float>();
            rebuildCombined();
        }
        commitPending();
        dryCopy.setSize (2, maxBlock, false, false, true);
        userOut.setSize (2, maxBlock, false, false, true);
        reset();
    }

    void reset()
    {
        modelConv.reset();
        userConv.reset();
        for (auto& f : cuts) f = {};
        onGain.reset (fs, 0.02);
        onGain.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        userMix.reset (fs, 0.03);
        userMix.setCurrentAndTargetValue (settings.type == ir && userLoaded ? 1.0f : 0.0f);
        level.reset (fs, 0.03);
        level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
    }

    /** Loads what is pending into the convolutions and prepares them, so the IRs are active from the
        very next process() call. Only while audio is stopped (prepare, or right after it). */
    void commitPending()
    {
        pickUp();
        const juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
        modelConv.prepare (spec);
        userConv.prepare (spec);
        // prepare() installs the IR at once, without a cross-fade from the empty engine
        userSettle = 0;
        userEngineReady = userLoaded && userConv.getCurrentIRSize() == userSize;
    }

    void setParams (const Settings& s) noexcept { settings = s; }
    const Settings& getParams() const noexcept { return settings; }

    //==============================================================================
    /** Hands over a modelled cabinet IR (message thread; built with designIR at the current rate). */
    void setModelIR (juce::AudioBuffer<float>&& irBuffer, double irRate)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingModel = std::move (irBuffer);
        pendingModelRate = irRate;
        modelPending = true;
    }
    /** A loaded cabinet IR in slot A (0) or B (1), one or two channels at the file's rate (message
        thread). It is kept, and resampled to the host rate whenever that changes; levelled so its
        mid band sits at 0 dB. With both slots loaded the convolution runs their MIX as one IR
        (B time-aligned to A), so two IRs cost what one does. */
    void setUserIR (juce::AudioBuffer<float>&& irBuffer, double irRate, int slot = 0)
    {
        const auto k = (size_t) juce::jlimit (0, 1, slot);
        const juce::ScopedLock sl (buildLock);
        userHost[k] = buildUserIR (irBuffer, irRate, fs);
        userSource[k] = std::move (irBuffer);
        userSourceRate[k] = irRate;
        rebuildCombined();
    }

    /** MIX of the two slots (0 = A only .. 1 = B only) and B's polarity. Off the audio thread (the
        housekeeping thread); the combined IR is rebuilt (and cross-faded in) when they change. */
    void setUserMix (float mix, bool invertB)
    {
        const juce::ScopedLock sl (buildLock);
        if (std::abs (mix - irMix) < 0.004f && invertB == irInvertB)
            return;
        irMix = mix;
        irInvertB = invertB;
        if (userHost[0].getNumSamples() > 0 && userHost[1].getNumSamples() > 0)
            rebuildCombined();
    }

    /** Where B's first arrival lands against A's (samples, + = B later), found by cross-correlation. */
    static int alignmentLag (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double rate)
    {
        const int maxLag = (int) (0.003 * rate), window = juce::jmin ((int) (0.012 * rate), a.getNumSamples(), b.getNumSamples());
        int best = 0;
        double bestV = -1.0;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const int j = i + lag;
                if (j >= 0 && j < b.getNumSamples())
                    acc += (double) a.getSample (0, i) * b.getSample (0, j);
            }
            if (std::abs (acc) > bestV) { bestV = std::abs (acc); best = lag; }
        }
        return best;
    }

    /** The IR as the convolution runs it: two channels at `hostRate`, levelled. */
    static juce::AudioBuffer<float> buildUserIR (const juce::AudioBuffer<float>& source, double sourceRate, double hostRate)
    {
        auto ir = irtools::resample (source, sourceRate, hostRate);
        juce::AudioBuffer<float> st (2, ir.getNumSamples());
        for (int c = 0; c < 2; ++c)
            st.copyFrom (c, 0, ir, juce::jmin (c, ir.getNumChannels() - 1), 0, ir.getNumSamples());
        levelIR (st, hostRate);
        return st;
    }
    void clearUserIR (int slot = 0)
    {
        const auto k = (size_t) juce::jlimit (0, 1, slot);
        const juce::ScopedLock sl (buildLock);
        userSource[k].setSize (0, 0);
        userHost[k].setSize (0, 0);
        rebuildCombined();
    }
    bool hasModelIR() const noexcept { return modelLoaded; }
    double getSampleRate() const noexcept { return fs; }

    //==============================================================================
    void process (float* const* audio, int numCh, int numSamples) noexcept
    {
        numCh = juce::jmin (numCh, 2);
        pickUp();

        onGain.setTargetValue (settings.on ? 1.0f : 0.0f);
        if (! settings.on && ! onGain.isSmoothing())
        {
            onGain.setCurrentAndTargetValue (0.0f);
            return;
        }

        for (int c = 0; c < numCh; ++c)
            dryCopy.copyFrom (c, 0, audio[c], numSamples);
        if (numCh == 1)
            dryCopy.clear (1, 0, numSamples);

        // a real IR is running (JUCE cross-fades from one IR to the next itself); before the first one: the model.
        // A fresh engine starts as a unit impulse and JUCE fades from it to the IR over 50 ms - that fade
        // would be the raw amp, so the IR counts as ready only once it is installed and the fade is over.
        if (! userEngineReady)
        {
            if (userLoaded && userConv.getCurrentIRSize() == userSize)
            {
                userSettle += numSamples;
                userEngineReady = userSettle >= (int) (0.06 * fs);
            }
            else
                userSettle = 0;
        }
        const bool userReady = userLoaded && userEngineReady;
        userMix.setTargetValue (settings.type == ir && userReady ? 1.0f : 0.0f);
        // a new IR only takes over inside process(): keep the engine running until it has (silently)
        const bool userInstalling = userLoaded && ! userReady;
        const bool runUser = userMix.getTargetValue() > 0.5f || userMix.isSmoothing() || userInstalling;
        const bool runModel = userMix.getTargetValue() < 0.5f || userMix.isSmoothing();

        if (runUser)
        {
            for (int c = 0; c < 2; ++c)
                userOut.copyFrom (c, 0, dryCopy, c, 0, numSamples);
            juce::dsp::AudioBlock<float> b (userOut.getArrayOfWritePointers(), 2, (size_t) numSamples);
            userConv.process (juce::dsp::ProcessContextReplacing<float> (b));
        }
        if (runModel)
        {
            juce::dsp::AudioBlock<float> b (audio, (size_t) numCh, (size_t) numSamples);
            if (modelLoaded)
                modelConv.process (juce::dsp::ProcessContextReplacing<float> (b));
            if (! modelLoaded || modelConv.getCurrentIRSize() < 16)
                b.clear();   // no cabinet running yet: silence, never the raw amp
        }
        if (runUser)
            for (int i = 0; i < numSamples; ++i)
            {
                const float m = userMix.getNextValue();
                for (int c = 0; c < numCh; ++c)
                    audio[c][i] = runModel ? audio[c][i] + m * (userOut.getSample (c, i) - audio[c][i]) : userOut.getSample (c, i);
            }

        // LOW CUT / HIGH CUT (12 dB/oct), LEVEL, on / off fade
        updateCuts();
        level.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
        const bool loOff = loCur <= 20.5f, hiOff = hiCur >= 19900.0f;   // at the ends of their ranges the cuts are off
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue(), on = onGain.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                float x = audio[c][i];
                if (! loOff)
                    x = cuts[0].process (x, c);
                if (! hiOff)
                    x = cuts[1].process (x, c);
                const float dry = dryCopy.getSample (c, i);
                audio[c][i] = dry + on * (x * g - dry);
            }
        }
    }

    //==============================================================================
    /** Magnitude of a modelled cabinet in dB (0 dB around 1 kHz): the smooth curve, its breakup ripple. */
    static float responseDb (int type, float mic, float distance, float hz) noexcept
    {
        type = juce::jlimit (0, (int) ir - 1, type);
        return (float) (curveDb (type, mic, distance, hz) + rippleDb (type, mic, hz));
    }

    /** A modelled cabinet's impulse response at `sampleRate` (message thread: allocates, FFTs).
        The speaker + mic part is minimum phase; on top come what a close mic also hears - the back panel
        (closed cabs) or the rear wave off the wall (open backs), the box's diffuse decay - and, pulled back,
        the floor and the room. Two passes pull the smoothed response back onto the curve. */
    static juce::AudioBuffer<float> designIR (int type, float mic, float distance, double sampleRate)
    {
        type = juce::jlimit (0, (int) ir - 1, type);
        const int order = sampleRate > 100000.0 ? 14 : 13;
        const int n = 1 << order;
        const int length = juce::jmin (n, (int) (0.09 * sampleRate));
        const float d = juce::jlimit (0.0f, 1.0f, distance);

        std::vector<double> targetDb ((size_t) n / 2 + 1), designDb ((size_t) n / 2 + 1);
        for (int k = 0; k <= n / 2; ++k)
        {
            const double hz = juce::jmax (1.0, (double) k * sampleRate / n);
            targetDb[(size_t) k] = curveDb (type, mic, distance, hz);
            designDb[(size_t) k] = targetDb[(size_t) k] + rippleDb (type, mic, hz);
        }

        juce::AudioBuffer<float> out (1, length);
        for (int pass = 0; pass < 3; ++pass)
        {
            auto direct = minimumPhase (designDb, n);
            out.clear();
            auto* h = out.getWritePointer (0);
            for (int i = 0; i < length; ++i)
                h[i] = (float) direct[(size_t) i];
            addBoxAndRoom (h, length, type, d, sampleRate);
            const int fadeFrom = (int) (length * 0.7);
            for (int i = fadeFrom; i < length; ++i)
                h[i] *= 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - fadeFrom) / (float) (length - fadeFrom));
            if (pass == 2)
            {
                // the same loudness for every cabinet and mic position: the 200 Hz..3 kHz band at kModelLevelDb
                levelIR (out, sampleRate);
                out.applyGain (juce::Decibels::decibelsToGain (kModelLevelDb));
                break;
            }
            // what came out, smoothed over 1/3 octave, against the target: correct the design by the difference
            std::vector<std::complex<double>> x ((size_t) n);
            for (int i = 0; i < length; ++i) x[(size_t) i] = h[i];
            fft (x, false);
            std::vector<double> power ((size_t) n / 2 + 1);
            for (int k = 0; k <= n / 2; ++k) power[(size_t) k] = std::norm (x[(size_t) k]);
            std::vector<double> prefix ((size_t) n / 2 + 2, 0.0);
            for (int k = 0; k <= n / 2; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + power[(size_t) k];
            double refErr = 0.0;
            std::vector<double> err ((size_t) n / 2 + 1, 0.0);
            for (int k = 1; k <= n / 2; ++k)
            {
                const int lo = juce::jmax (1, (int) std::floor (k * 0.891)), hi = juce::jmin (n / 2, (int) std::ceil (k * 1.122));
                const double smoothDb = 10.0 * std::log10 ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1) + 1.0e-24);
                err[(size_t) k] = smoothDb - targetDb[(size_t) k];
            }
            err[0] = err[1];
            // only the shape matters (the level is set below): relative to the 1 kHz region
            const int k1 = (int) std::round (1000.0 * n / sampleRate);
            refErr = err[(size_t) k1];
            for (int k = 0; k <= n / 2; ++k)
            {
                const double hz = (double) k * sampleRate / n;
                if (hz > 25.0 && hz < 16000.0)
                    designDb[(size_t) k] -= juce::jlimit (-12.0, 12.0, err[(size_t) k] - refErr);
            }
        }
        return out;
    }

    static constexpr float kModelLevelDb = -5.0f;   // modelled cabinets: about as loud as the cabinets of v3.1 beta 1

    /** Levels a loaded IR: its 200 Hz..3 kHz band averages 0 dB (message thread). */
    static void levelIR (juce::AudioBuffer<float>& irBuf, double sampleRate)
    {
        const int len = irBuf.getNumSamples();
        if (len == 0) return;
        int n = 1;
        while (n < len * 2) n <<= 1;
        double sum = 0.0;
        int count = 0;
        for (int c = 0; c < irBuf.getNumChannels(); ++c)
        {
            std::vector<std::complex<double>> x ((size_t) n);
            for (int i = 0; i < len; ++i)
                x[(size_t) i] = irBuf.getSample (c, i);
            fft (x, false);
            for (int k = 1; k < n / 2; ++k)
            {
                const double hz = (double) k * sampleRate / n;
                if (hz >= 200.0 && hz <= 3000.0)
                {
                    sum += 20.0 * std::log10 (std::abs (x[(size_t) k]) + 1.0e-12);
                    ++count;
                }
            }
        }
        if (count > 0)
            irBuf.applyGain ((float) std::pow (10.0, -(sum / count) / 20.0));
    }

private:
    static constexpr int kPoints = 57;
    static constexpr float open1x12Db[kPoints] { -10.5f, -9.5f, -7.7f, -5.8f, -4.0f, -2.4f, -0.9f, 0.4f, 1.3f, 2.0f, 2.3f, 2.4f, 2.1f, 1.8f, 1.6f, 1.7f, 2.2f, 2.6f, 2.7f, 2.5f, 2.3f, 2.5f, 2.5f, 2.2f, 2.1f, 1.8f, 2.1f, 1.9f, 2.0f, 1.2f, 0.4f, -0.4f, -1.1f, -2.1f, -2.7f, -2.2f, -1.9f, -1.8f, -2.3f, -2.1f, -1.3f, -0.4f, -1.5f, -4.4f, -8.2f, -10.4f, -12.5f, -16.3f, -20.8f, -24.7f, -26.7f, -27.5f, -29.2f, -31.9f, -36.4f, -40.9f, -43.3f };
    static constexpr float open2x12Db[kPoints] { -8.4f, -7.3f, -5.3f, -3.3f, -1.5f, 0.1f, 1.5f, 2.7f, 3.4f, 3.8f, 3.7f, 3.5f, 3.0f, 2.6f, 2.3f, 2.5f, 3.1f, 3.8f, 4.0f, 3.9f, 3.7f, 3.5f, 3.2f, 2.9f, 2.6f, 2.3f, 1.9f, 1.1f, 0.5f, 0.1f, -0.3f, -0.7f, -1.4f, -2.1f, -2.0f, -1.4f, -0.3f, 0.1f, -0.1f, -0.5f, -0.6f, -0.2f, -0.6f, -2.4f, -6.0f, -9.6f, -13.8f, -17.5f, -21.6f, -23.9f, -25.6f, -27.5f, -30.9f, -35.5f, -40.0f, -43.8f, -45.3f };
    static constexpr float brit4x12Db[kPoints] { -20.4f, -18.8f, -15.9f, -13.2f, -10.6f, -7.8f, -4.5f, -1.6f, 0.9f, 2.9f, 4.6f, 6.1f, 6.9f, 7.5f, 7.6f, 7.4f, 6.9f, 6.3f, 5.6f, 4.9f, 4.1f, 3.5f, 3.0f, 2.3f, 1.1f, 0.4f, 0.8f, 1.5f, 1.4f, 0.1f, -1.0f, -1.4f, -1.7f, -1.9f, -1.7f, -1.2f, 0.3f, 1.6f, 3.0f, 3.0f, 2.8f, 1.8f, 0.7f, -1.4f, -3.1f, -6.5f, -11.4f, -16.8f, -20.4f, -22.8f, -25.3f, -27.3f, -28.1f, -28.5f, -30.9f, -35.1f, -37.8f };
    static constexpr float modern4x12Db[kPoints] { -20.1f, -18.5f, -15.6f, -12.9f, -10.3f, -7.4f, -4.7f, -2.2f, -0.2f, 1.8f, 3.4f, 4.9f, 5.7f, 5.9f, 5.9f, 5.7f, 5.4f, 5.1f, 4.7f, 4.2f, 3.6f, 2.9f, 2.2f, 1.5f, 0.4f, -0.2f, 0.2f, 0.7f, 0.6f, -1.0f, -1.9f, -1.5f, -1.4f, -1.3f, -1.3f, -0.4f, 1.3f, 2.2f, 1.9f, 0.8f, 0.5f, 0.5f, -0.0f, -1.5f, -4.0f, -8.7f, -14.5f, -20.0f, -23.7f, -25.4f, -27.1f, -28.6f, -31.5f, -34.6f, -38.2f, -41.4f, -42.9f };
    static constexpr float micEdgeDb[kPoints] { 3.4f, 3.4f, 3.4f, 3.4f, 3.3f, 3.2f, 3.2f, 3.1f, 3.1f, 3.0f, 3.0f, 2.9f, 2.9f, 2.9f, 2.8f, 2.7f, 2.7f, 2.6f, 2.4f, 2.2f, 2.3f, 2.1f, 1.8f, 1.9f, 2.1f, 2.2f, 2.5f, 3.1f, 3.1f, 2.9f, 1.9f, 0.5f, -1.4f, -2.5f, -3.7f, -3.6f, -3.2f, -3.4f, -4.1f, -5.6f, -7.4f, -8.3f, -7.4f, -9.0f, -9.8f, -8.3f, -7.8f, -7.6f, -6.9f, -5.9f, -7.6f, -10.2f, -11.2f, -9.7f, -8.6f, -7.7f, -4.9f };
    static constexpr float distRoomDb[kPoints] { -3.7f, -3.8f, -3.8f, -3.6f, -3.6f, -3.5f, -3.4f, -3.2f, -3.0f, -3.0f, -2.9f, -2.8f, -2.6f, -2.5f, -2.2f, -2.0f, -1.8f, -1.7f, -1.3f, -0.9f, -0.8f, -0.7f, -0.5f, -1.2f, -1.9f, -2.6f, -2.4f, -2.5f, -2.2f, -1.6f, -0.7f, -0.9f, -0.2f, 1.0f, 1.5f, 2.3f, 3.0f, 2.6f, 2.1f, 2.0f, 1.9f, 1.8f, 2.7f, 2.6f, 2.3f, 1.9f, 1.5f, 0.2f, 1.0f, 3.0f, 3.8f, 4.1f, 5.1f, 4.8f, 2.7f, 2.1f, 2.1f };

    /** The smooth curve of a cabinet class (averaged from many real cabinets of that kind, 1/6 octave),
        with the mic (cap .. edge) and distance (on the grille .. a few inches back) trends of a real 4x12. */
    static double curveDb (int type, float mic, float distance, double hz) noexcept
    {
        const float* base = type == open1x12 ? open1x12Db : type == open2x12 ? open2x12Db : type == brit4x12 ? brit4x12Db : modern4x12Db;
        auto at = [] (const float* t, double f)
        {
            const double pos = std::log2 (juce::jmax (1.0, f) / 30.0) * 6.0;
            if (pos <= 0.0) return (double) t[0] + 2.0 * pos;                          // -12 dB / octave below 30 Hz
            if (pos >= kPoints - 1) return (double) t[kPoints - 1] - 4.0 * (pos - (kPoints - 1));   // -24 dB / octave on top
            const int i = (int) pos;
            const double fr = pos - i;
            return (double) t[i] + fr * (t[i + 1] - t[i]);
        };
        const double m = juce::jlimit (0.0f, 1.0f, mic), d = juce::jlimit (0.0f, 1.0f, distance);
        const double close = juce::jmin (1.0, d / 0.6);
        return at (base, hz) + 1.1 * (m - 0.3) * at (micEdgeDb, hz) + (close - 0.3) * at (distRoomDb, hz) + topLiftDb (hz);
    }

    /** The cabinets' top against the median of ~250 real guitar-cab IRs: 5..10 kHz came out 4-6 dB
        dark (a dull top makes the mids stick out - boxy, nasal), so it is lifted back. */
    static double topLiftDb (double hz) noexcept
    {
        static const double pts[][2] { { 4000.0, 0.0 }, { 5000.0, 3.0 }, { 6300.0, 4.5 }, { 8000.0, 4.0 }, { 10000.0, 3.0 }, { 14000.0, 0.0 } };
        if (hz <= pts[0][0] || hz >= pts[5][0])
            return 0.0;
        int i = 0;
        while (hz > pts[i + 1][0]) ++i;
        const double t = std::log (hz / pts[i][0]) / std::log (pts[i + 1][0] / pts[i][0]);
        return pts[i][1] + t * (pts[i + 1][1] - pts[i][1]);
    }

    static double bellDb (double hz, double f0, double q, double db) noexcept
    {
        const double r = hz / f0 - f0 / hz;
        return db / (1.0 + q * q * r * r);
    }

    /** Cone breakup: a fixed pattern of narrow peaks and dips of each speaker, smoother towards the edge. */
    static double rippleDb (int type, float mic, double hz) noexcept
    {
        juce::Random rng (7 + 31 * type);
        const double amount = 6.0 * (1.0 - 0.4 * juce::jlimit (0.0f, 1.0f, mic));   // real IRs: ~2 dB rms, peaks ~5 dB
        double db = 0.0;
        for (int k = 0; k < 24; ++k)
        {
            const double f0 = 700.0 * std::pow (2.0, 3.6 * (double) k / 24.0 + 0.12 * rng.nextDouble());
            const double g = amount * (rng.nextBool() ? 1.0 : -1.0) * (0.4 + 0.6 * rng.nextDouble());
            db += bellDb (hz, f0, 6.0 + 8.0 * rng.nextDouble(), g);
        }
        return db;
    }

    /** Minimum-phase impulse response for a log magnitude on the FFT grid (real cepstrum). */
    static std::vector<double> minimumPhase (const std::vector<double>& db, int n)
    {
        std::vector<std::complex<double>> x ((size_t) n);
        for (int k = 0; k <= n / 2; ++k)
        {
            const double lnMag = juce::jmax (-100.0, db[(size_t) k]) * (std::log (10.0) / 20.0);
            x[(size_t) k] = lnMag;
            if (k > 0 && k < n / 2)
                x[(size_t) (n - k)] = lnMag;
        }
        fft (x, true);
        for (int k = 1; k < n / 2; ++k) x[(size_t) k] *= 2.0;
        for (int k = n / 2 + 1; k < n; ++k) x[(size_t) k] = 0.0;
        fft (x, false);
        for (auto& v : x) v = std::exp (v);
        fft (x, true);
        std::vector<double> h ((size_t) n);
        for (int i = 0; i < n; ++i) h[(size_t) i] = x[(size_t) i].real();
        return h;
    }

    /** What the mic hears besides the cone: the box (or the wall behind an open back), its decay, the room. */
    static void addBoxAndRoom (float* h, int length, int type, float d, double sr)
    {
        const std::vector<float> direct (h, h + length);
        auto echo = [&] (double ms, float gain, double lpHz)
        {
            const int delay = (int) (ms * 0.001 * sr);
            const float a = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * lpHz / sr));
            float lp = 0.0f;
            for (int i = 0; i + delay < length; ++i)
            {
                lp += a * (direct[(size_t) i] - lp);
                h[i + delay] += gain * lp;
            }
        };
        const bool open = type == open1x12 || type == open2x12;
        if (open)
        {
            echo (type == open1x12 ? 3.4 : 4.1, -0.30f, 2200.0);   // the rear wave, off the wall behind the amp
            echo (7.9, 0.10f, 1500.0);
            echo (0.71, 0.11f, 7000.0);                             // baffle edges and the cabinet sides
            echo (1.13, -0.09f, 6000.0);
        }
        else
        {
            echo (type == brit4x12 ? 1.7 : 1.45, 0.26f, 2000.0);  // the back panel, through the cone
            echo (3.1, 0.12f, 1200.0);
            echo (0.52, 0.12f, 7000.0);                             // the neighbouring speakers, the baffle, the sides
            echo (0.87, -0.10f, 6000.0);
            echo (1.21, 0.09f, 5000.0);
            echo (2.3, -0.07f, 3000.0);
        }
        // the box's diffuse decay (panels and the air inside), band-limited
        juce::Random rng (99 + type);
        const float tau = (float) ((open ? 0.011 : 0.019) * sr);
        float peak = 0.0f;
        for (int i = 0; i < length; ++i) peak = juce::jmax (peak, std::abs (direct[(size_t) i]));
        const float tail = peak * (open ? 0.018f : 0.028f);
        float lp = 0.0f, lp2 = 0.0f;
        const float aLp = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 3200.0 / sr));
        const float aHp = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 160.0 / sr));
        const int start = (int) (0.0006 * sr);
        for (int i = start; i < length; ++i)
        {
            lp += aLp * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            lp2 += aHp * (lp - lp2);
            h[i] += tail * (lp - lp2) * std::exp (-(float) (i - start) / tau);
        }
        // further back: the floor bounce and some room
        if (d > 0.45f)
        {
            const float room = (d - 0.45f) / 0.55f;
            const std::vector<float> near (h, h + length);
            const int bounce = (int) ((1.0 + 3.0 * room) * 0.001 * sr);
            const float bounceGain = 0.35f * room;
            for (int i = bounce + 2; i < length; ++i)
                h[i] += bounceGain * (0.25f * near[(size_t) (i - bounce)] + 0.5f * near[(size_t) (i - bounce - 1)] + 0.25f * near[(size_t) (i - bounce - 2)]);
            float air = 0.0f;
            const float airTau = (float) (0.02 * sr);
            for (int i = bounce; i < length; ++i)
            {
                air += 0.3f * ((rng.nextFloat() * 2.0f - 1.0f) - air);
                h[i] += peak * 0.03f * room * air * std::exp (-(float) (i - bounce) / airTau);
            }
        }
    }

    /** In-place radix-2 FFT (double; inverse scaled by 1/n). */
    static void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * juce::MathConstants<double>::pi / (double) len * (inverse ? 1.0 : -1.0);
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse)
            for (auto& v : a) v /= (double) n;
    }

    //==============================================================================
    struct Cut
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1 {}, z2 {};
        float hz = -1.0f;
        void set (bool high, double f, double sr) noexcept
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, f) / sr;
            const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071), a0 = 1.0 + alpha;
            if (high) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
            else      { b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; }
            b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
        }
        inline float process (float xf, int c) noexcept
        {
            const double x = xf, y = b0 * x + z1[(size_t) c];
            z1[(size_t) c] = b1 * x - a1 * y + z2[(size_t) c];
            z2[(size_t) c] = b2 * x - a2 * y;
            return (float) y;
        }
    };

    void updateCuts() noexcept
    {
        const float lo = juce::jlimit (20.0f, 1000.0f, settings.lowCutHz), hi = juce::jlimit (1000.0f, 20000.0f, settings.highCutHz);
        // a light glide so the cut sweeps without zipper noise
        auto glide = [] (float& cur, float target) { cur = cur < 0.0f ? target : cur * std::pow (target / cur, 0.5f); };
        glide (loCur, lo);
        glide (hiCur, hi);
        if (std::abs (cuts[0].hz - loCur) > 0.01f) { cuts[0].set (true, loCur, fs); cuts[0].hz = loCur; }
        const bool hiOff = hiCur >= 19900.0f;
        if (std::abs (cuts[1].hz - hiCur) > 0.1f) { cuts[1].set (false, hiOff ? fs * 0.45 : hiCur, fs); cuts[1].hz = hiCur; }
    }

    /** The IR the convolution runs, from the slots (caller holds buildLock). */
    void rebuildCombined()
    {
        const auto& a = userHost[0];
        const auto& b = userHost[1];
        juce::AudioBuffer<float> out;
        if (a.getNumSamples() > 0 && b.getNumSamples() > 0)
        {
            // B moved so its arrival lines up with A's (so the mix does not comb-filter), then A x (1 - MIX) + B x MIX
            const int lag = alignmentLag (a, b, fs);
            const int len = juce::jmax (a.getNumSamples(), b.getNumSamples() - lag);
            out.setSize (2, juce::jmax (16, len));
            out.clear();
            const float ga = 1.0f - irMix, gb = irMix * (irInvertB ? -1.0f : 1.0f);
            for (int c = 0; c < 2; ++c)
            {
                out.addFrom (c, 0, a, c, 0, a.getNumSamples(), ga);
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    const int j = i + lag;
                    if (j >= 0 && j < b.getNumSamples())
                        out.getWritePointer (c)[i] += gb * b.getSample (c, j);
                }
            }
        }
        else if (a.getNumSamples() > 0 || b.getNumSamples() > 0)
        {
            out.makeCopyOf (a.getNumSamples() > 0 ? a : b);
            if (a.getNumSamples() == 0 && irInvertB)
                out.applyGain (-1.0f);
        }
        const juce::SpinLock::ScopedLockType sl (lock);
        if (out.getNumSamples() == 0)
        {
            userCleared = true;
            userPending = false;
            return;
        }
        pendingUser = std::move (out);
        pendingUserRate = fs;
        userPending = true;
        userCleared = false;
    }

    void pickUp() noexcept
    {
        const juce::GenericScopedTryLock<juce::SpinLock> sl (lock);
        if (! sl.isLocked())
            return;
        if (modelPending)
        {
            modelConv.loadImpulseResponse (std::move (pendingModel), pendingModelRate, juce::dsp::Convolution::Stereo::no,
                                           juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            modelPending = false;
            modelLoaded = true;
        }
        if (userPending)
        {
            userSize = pendingUser.getNumSamples();
            userConv.loadImpulseResponse (std::move (pendingUser), pendingUserRate, juce::dsp::Convolution::Stereo::yes,
                                          juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            userPending = false;
            userLoaded = true;
        }
        if (userCleared)
        {
            userCleared = false;
            userLoaded = false;
            // the engine still holds the cleared IR: the next one has to finish its fade before it plays
            userEngineReady = false;
            userSettle = 0;
        }
    }

    Settings settings;
    double fs = 44100.0;
    int maxBlock = 512;
    juce::dsp::Convolution modelConv, userConv;
    juce::AudioBuffer<float> dryCopy, userOut;
    std::array<Cut, 2> cuts;
    float loCur = -1.0f, hiCur = -1.0f;
    juce::SmoothedValue<float> onGain, userMix, level;

    juce::SpinLock lock;
    juce::AudioBuffer<float> pendingModel, pendingUser;
    // the two IR slots: as loaded (resampled again on a rate change) and at the host rate, levelled
    juce::CriticalSection buildLock;
    std::array<juce::AudioBuffer<float>, 2> userSource, userHost;
    std::array<double, 2> userSourceRate { 44100.0, 44100.0 };
    float irMix = 0.5f;
    bool irInvertB = false;
    int userSize = -1, userSettle = 0;
    bool userEngineReady = false;
    double pendingModelRate = 44100.0, pendingUserRate = 44100.0;
    bool modelPending = false, userPending = false, userCleared = false;
    bool modelLoaded = false, userLoaded = false;
};
