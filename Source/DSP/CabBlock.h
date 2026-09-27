#pragma once

#include <JuceHeader.h>

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
 * both engines (model / user IR) cross-fade when switching.
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
        modelConv.prepare (spec);
        userConv.prepare (spec);
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
    /** A loaded cabinet IR (message thread). Levelled so its mid band sits at 0 dB. */
    void setUserIR (juce::AudioBuffer<float>&& irBuffer, double irRate)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingUser = std::move (irBuffer);
        pendingUserRate = irRate;
        userPending = true;
    }
    void clearUserIR()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        userCleared = true;
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

        userMix.setTargetValue (settings.type == ir && userLoaded ? 1.0f : 0.0f);
        const bool runUser = userMix.getTargetValue() > 0.5f || userMix.isSmoothing();
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
            if (modelLoaded)
            {
                juce::dsp::AudioBlock<float> b (audio, (size_t) numCh, (size_t) numSamples);
                modelConv.process (juce::dsp::ProcessContextReplacing<float> (b));
            }
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
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue(), on = onGain.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                float x = audio[c][i];
                x = cuts[0].process (x, c);
                x = cuts[1].process (x, c);
                const float dry = dryCopy.getSample (c, i);
                audio[c][i] = dry + on * (x * g - dry);
            }
        }
    }

    //==============================================================================
    /** Magnitude of a modelled cabinet in dB (0 dB at 800 Hz), before the room reflection. */
    static float responseDb (int type, float mic, float distance, float hz) noexcept
    {
        const auto sh = shapeFor (type, mic, distance);
        return (float) (shapeDb (sh, hz) - shapeDb (sh, 800.0));
    }

    /** A modelled cabinet's impulse response at `sampleRate` (message thread: allocates, FFTs). */
    static juce::AudioBuffer<float> designIR (int type, float mic, float distance, double sampleRate)
    {
        type = juce::jlimit (0, (int) ir - 1, type);
        const auto sh = shapeFor (type, mic, distance);
        const int order = sampleRate > 100000.0 ? 14 : 13;
        const int n = 1 << order;

        // log magnitude on the FFT grid
        std::vector<std::complex<double>> x ((size_t) n);
        const double ref = shapeDb (sh, 800.0);
        for (int k = 0; k <= n / 2; ++k)
        {
            const double hz = juce::jmax (1.0, (double) k * sampleRate / n);
            const double db = juce::jmax (-80.0, shapeDb (sh, hz) - ref);
            const double lnMag = db * (std::log (10.0) / 20.0);
            x[(size_t) k] = lnMag;
            if (k > 0 && k < n / 2)
                x[(size_t) (n - k)] = lnMag;
        }
        // minimum phase through the real cepstrum
        fft (x, true);
        for (int k = 1; k < n / 2; ++k)
            x[(size_t) k] *= 2.0;
        for (int k = n / 2 + 1; k < n; ++k)
            x[(size_t) k] = 0.0;
        fft (x, false);
        for (auto& v : x)
            v = std::exp (v);
        fft (x, true);

        // ~50 ms, faded out, plus the room: floor bounce and a little diffuse air when miked from afar
        const int length = juce::jmin (n, (int) (0.05 * sampleRate));
        juce::AudioBuffer<float> out (1, length);
        out.clear();
        auto* h = out.getWritePointer (0);
        const int fadeFrom = (int) (length * 0.6);
        for (int i = 0; i < length; ++i)
        {
            float w = 1.0f;
            if (i > fadeFrom)
                w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - fadeFrom) / (float) (length - fadeFrom));
            h[i] = (float) x[(size_t) i].real() * w;
        }

        const float d = juce::jlimit (0.0f, 1.0f, distance);
        if (d > 0.0f)
        {
            std::vector<float> dry (h, h + length);
            const int bounce = (int) ((0.35 + 3.2 * d) * 0.001 * sampleRate);
            const float bounceGain = 0.18f + 0.4f * d;
            for (int i = bounce + 2; i < length; ++i)   // floor bounce, slightly dull
                h[i] += bounceGain * (0.25f * dry[(size_t) (i - bounce)] + 0.5f * dry[(size_t) (i - bounce - 1)] + 0.25f * dry[(size_t) (i - bounce - 2)]);
            juce::Random rng (4242 + type);
            const float air = 0.02f * d * d;
            float lp = 0.0f;
            const float tau = (float) (0.012 * sampleRate);
            for (int i = bounce; i < length; ++i)
            {
                lp += 0.35f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
                h[i] += air * lp * std::exp (-(float) (i - bounce) / tau);
            }
            // the reflections make it louder: keep the 800 Hz level where it was
            out.applyGain (1.0f / (1.0f + 0.5f * bounceGain));
        }
        return out;
    }

    /** Levels a loaded IR: its 200 Hz..3 kHz band averages 0 dB (message thread). */
    static void levelIR (juce::AudioBuffer<float>& irBuf, double sampleRate)
    {
        const int len = irBuf.getNumSamples();
        if (len == 0) return;
        int n = 1;
        while (n < len * 2) n <<= 1;
        std::vector<std::complex<double>> x ((size_t) n);
        for (int i = 0; i < len; ++i)
            x[(size_t) i] = irBuf.getSample (0, i);
        fft (x, false);
        double sum = 0.0;
        int count = 0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double hz = (double) k * sampleRate / n;
            if (hz >= 200.0 && hz <= 3000.0)
            {
                sum += 20.0 * std::log10 (std::abs (x[(size_t) k]) + 1.0e-12);
                ++count;
            }
        }
        if (count > 0)
            irBuf.applyGain ((float) std::pow (10.0, -(sum / count) / 20.0));
    }

private:
    struct Shape
    {
        float resHz, resQ;           // cone in the box
        float openHz;                // open back: extra first-order low cut (0 = closed)
        float lowMidHz, lowMidDb, lowMidQ;
        float presHz, presDb, presQ;
        float rippleFromHz, rippleDb; // cone breakup
        float rollHz; int rollOrder;
        float notchHz, notchDb;
        float proximityDb;           // close-miking bass lift
        int seed;
    };

    static Shape shapeFor (int type, float mic, float distance) noexcept
    {
        Shape s;
        switch (type)
        {
            case open1x12:   s = { 88.0f, 0.9f, 115.0f, 380.0f, -1.0f, 1.0f, 2300.0f, 3.0f, 1.2f, 2600.0f, 2.5f, 5600.0f, 3, 4200.0f, -4.0f, 0.0f, 11 }; break;
            case open2x12:   s = { 95.0f, 1.0f,  90.0f, 420.0f, -1.5f, 1.0f, 1900.0f, 4.0f, 1.1f, 2400.0f, 3.0f, 5200.0f, 3, 3800.0f, -5.0f, 0.0f, 23 }; break;
            case brit4x12:   s = { 82.0f, 1.3f,   0.0f, 700.0f,  1.0f, 0.9f, 1500.0f, 4.0f, 1.0f, 2200.0f, 3.0f, 4300.0f, 4, 3400.0f, -4.0f, 0.0f, 37 }; break;
            default:         s = { 105.0f, 1.5f,  0.0f, 500.0f, -3.0f, 1.0f, 2600.0f, 6.0f, 1.4f, 3000.0f, 4.0f, 5000.0f, 4, 4400.0f, -6.0f, 0.0f, 51 }; break;
        }
        const float m = juce::jlimit (0.0f, 1.0f, mic), d = juce::jlimit (0.0f, 1.0f, distance);
        // mic towards the edge: the top rolls off earlier, less presence and fizz, a little warmer
        s.rollHz *= std::pow (2.0f, -0.7f * m);
        s.presDb -= 4.0f * m;
        s.presHz *= std::pow (2.0f, -0.4f * m);
        s.rippleDb *= 1.0f - 0.5f * m;
        s.lowMidDb += 1.5f * m;
        // distance: proximity bass up close, a darker top further away
        s.proximityDb = 5.0f * (1.0f - d) * (1.0f - d);
        s.rollHz *= std::pow (2.0f, -0.35f * d);
        return s;
    }

    static double bellDb (double hz, double f0, double q, double db) noexcept
    {
        const double r = hz / f0 - f0 / hz;
        return db / (1.0 + q * q * r * r);
    }

    static double shapeDb (const Shape& s, double hz) noexcept
    {
        const double w = hz / s.resHz;
        // second-order high-pass with Q: the resonance bump and the 12 dB/oct fall below it
        const double hp2 = 20.0 * std::log10 (w * w / std::sqrt ((1.0 - w * w) * (1.0 - w * w) + (w / s.resQ) * (w / s.resQ)));
        double db = hp2;
        if (s.openHz > 0.0f)
            db += 20.0 * std::log10 ((hz / s.openHz) / std::sqrt (1.0 + (hz / s.openHz) * (hz / s.openHz)));
        db += bellDb (hz, s.lowMidHz, s.lowMidQ, s.lowMidDb);
        db += bellDb (hz, s.presHz, s.presQ, s.presDb);
        db += bellDb (hz, s.notchHz, 3.0, s.notchDb);
        db += s.proximityDb / (1.0 + std::pow (hz / 160.0, 2.0));
        // cone breakup: a fixed pattern of narrow peaks and dips above rippleFrom
        juce::Random rng (s.seed);
        for (int k = 0; k < 9; ++k)
        {
            const double f0 = s.rippleFromHz * std::pow (2.0, 1.6 * (double) k / 9.0 + 0.1 * rng.nextDouble());
            const double g = s.rippleDb * (rng.nextBool() ? 1.0 : -0.8) * (0.5 + 0.5 * rng.nextDouble());
            db += bellDb (hz, f0, 5.0 + 4.0 * rng.nextDouble(), g);
        }
        // the top: a steep Butterworth-like roll-off
        db += -10.0 * std::log10 (1.0 + std::pow (hz / s.rollHz, 2.0 * s.rollOrder));
        return db;
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
            userConv.loadImpulseResponse (std::move (pendingUser), pendingUserRate, juce::dsp::Convolution::Stereo::no,
                                          juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            userPending = false;
            userLoaded = true;
        }
        if (userCleared)
        {
            userCleared = false;
            userLoaded = false;
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
    double pendingModelRate = 44100.0, pendingUserRate = 44100.0;
    bool modelPending = false, userPending = false, userCleared = false;
    bool modelLoaded = false, userLoaded = false;
};
