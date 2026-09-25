#include <algorithm>
// Offline, headless verification of the Swarmness DSP.
// Build target: SwarmnessTests   (run via `ctest` or directly)
//
// Optional: `SwarmnessTests --render <dir>` writes WAV renders of every factory preset.

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <chrono>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", condition ? " OK " : "FAIL", what.toRawUTF8());
        if (! condition)
            ++failures;
    }

    void setParam (SwarmnessAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.getAPVTS().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Guitar-ish test signal: decaying harmonic plucks plus a little noise. */
    juce::AudioBuffer<float> makeGuitar (double sr, int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (42);
        const float notes[] = { 82.41f, 110.0f, 146.83f, 196.0f };
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = i / sr;
            const int note = (int) (t / 0.5) % 4;
            const double tn = std::fmod (t, 0.5);
            const double env = std::exp (-tn * 4.0);
            double s = 0.0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (juce::MathConstants<double>::twoPi * notes[note] * h * tn) / (double) h;
            s = 0.3 * env * s + 0.002 * (rng.nextFloat() - 0.5f);
            b.setSample (0, i, (float) s);
            b.setSample (1, i, (float) (s * 0.95));
        }
        return b;
    }

    juce::AudioBuffer<float> makeSine (double sr, int numSamples, double freq, float amp = 0.25f)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr);
            b.setSample (0, i, s);
            b.setSample (1, i, s);
        }
        return b;
    }

    juce::AudioBuffer<float> render (SwarmnessAudioProcessor& p, const juce::AudioBuffer<float>& input, double sr, int blockSize)
    {
        p.prepareToPlay (sr, blockSize);
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        for (int start = 0; start < out.getNumSamples(); start += blockSize)
        {
            const int n = juce::jmin (blockSize, out.getNumSamples() - start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
            p.processBlock (view, midi);
        }
        return out;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    /** RMS of (a[n] - b[n - delay]) relative to RMS of a, in dB, over a window. */
    double nullDb (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int delay, int from, int to)
    {
        double err = 0.0, ref = 0.0;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = from; i < to; ++i)
            {
                const float x = i - delay >= 0 ? in.getSample (ch, i - delay) : 0.0f;
                const float d = out.getSample (ch, i) - x;
                err += d * d;
                ref += x * x;
            }
        return 10.0 * std::log10 ((err + 1.0e-20) / (ref + 1.0e-20));
    }

    /** Dominant frequency (Hz) via FFT with parabolic peak interpolation; also returns the purity in dB. */
    double dominantFrequency (const juce::AudioBuffer<float>& b, double sr, int start, double& purityDb)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data (size * 2, 0.0f);
        juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
        for (int i = 0; i < size && start + i < b.getNumSamples(); ++i)
            data[(size_t) i] = b.getSample (0, start + i);
        window.multiplyWithWindowingTable (data.data(), size);
        fft.performFrequencyOnlyForwardTransform (data.data());

        int peak = 1;
        for (int i = 2; i < size / 2; ++i)
            if (data[(size_t) i] > data[(size_t) peak])
                peak = i;

        double total = 0.0, near = 0.0;
        for (int i = 1; i < size / 2; ++i)
        {
            const double e = (double) data[(size_t) i] * data[(size_t) i];
            total += e;
            if (std::abs (i - peak) <= 6)
                near += e;
        }
        purityDb = 10.0 * std::log10 ((total - near + 1.0e-20) / (near + 1.0e-20));

        const double a = data[(size_t) peak - 1], c = data[(size_t) peak], d = data[(size_t) peak + 1];
        const double offset = 0.5 * (a - d) / (a - 2.0 * c + d);
        return (peak + offset) * sr / size;
    }

    void resetToInit (SwarmnessAudioProcessor& p)
    {
        p.getPresetManager().loadPreset ("Init");
    }

    //==========================================================================
    /** Renders with the NOISE footswitch held for the middle half of the input (like a player would). */
    juce::AudioBuffer<float> renderWithFootswitch (SwarmnessAudioProcessor& p, const juce::AudioBuffer<float>& input,
                                                   double sr, int blockSize, const char* switchId, bool magic = false)
    {
        p.prepareToPlay (sr, blockSize);
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        const int n = out.getNumSamples();
        for (int start = 0; start < n; start += blockSize)
        {
            const bool held = start > n / 4 && start < (3 * n) / 4;
            if (switchId != nullptr) setParam (p, switchId, held ? 1.0f : 0.0f);
            if (magic)               setParam (p, ParamIDs::magicHold, held ? 1.0f : 0.0f);
            const int len = juce::jmin (blockSize, n - start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, len);
            p.processBlock (view, midi);
        }
        return out;
    }

    void testPresetsStable()
    {
        std::printf ("\nFactory presets (with footswitches played): stability at several sample rates / block sizes\n");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int block : { 64, 333, 1024 })
            {
                SwarmnessAudioProcessor p;
                auto input = makeGuitar (sr, (int) (sr * 2.0));
                bool ok = true;
                float peak = 0.0f;
                for (const auto& name : p.getPresetManager().getFactoryPresetNames())
                {
                    for (const char* sw : { ParamIDs::oct1, ParamIDs::oct2 })
                    {
                        p.getPresetManager().loadPreset (name);
                        setParam (p, ParamIDs::flowSync, 0.0f);   // no playhead offline
                        auto out = renderWithFootswitch (p, input, sr, block, sw, true);
                        ok = ok && allFinite (out);
                        peak = juce::jmax (peak, out.getMagnitude (0, out.getNumSamples()));
                    }
                }
                check (ok && peak <= 2.0f, juce::String::formatted ("sr %.0f, block %d: finite, peak %.2f", sr, block, peak));
            }
        }
    }

    void testBypassNull()
    {
        std::printf ("\nBypass is sample-accurate and latency compensated\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Hive Collapse");
        setParam (p, ParamIDs::bypass, 1.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        auto out = render (p, input, sr, 256);
        const int latency = p.getLatencySamples();
        const double db = nullDb (out, input, latency, 4096, input.getNumSamples());
        check (db < -120.0, juce::String::formatted ("bypass null %.1f dB (latency %d samples)", db, latency));
    }

    void testFootswitchesOverrideBypass()
    {
        std::printf ("\nFootswitches work like momentary pedals (even when bypassed / RAINBOW off)\n");
        const double sr = 48000.0;
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::stingRaw, 0.0f);   // exact pitch check: modern engine
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::bypass, 1.0f);
            setParam (p, ParamIDs::oct1, 1.0f);
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            double purity = 0.0;
            const double f = dominantFrequency (out, sr, 48000, purity);
            check (std::abs (1200.0 * std::log2 (f / 440.0)) < 5.0,
                   juce::String::formatted ("bypassed + hold +1 OCT: %.2f Hz (expected 440)", f));
        }
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);                     // RAINBOW off
            setParam (p, ParamIDs::bypass, 1.0f);
            setParam (p, ParamIDs::magicHold, 1.0f);
            auto input = makeGuitar (sr, 48000);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 12000, input.getNumSamples());
            check (db > -20.0, juce::String::formatted ("bypassed + RAINBOW off + hold MAGIC: effect audible (residual %.1f dB)", db));
        }
    }

    void testVenomLink()
    {
        std::printf ("\nLINK: the VENOM footswitch drags the linked octave in\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::stingRaw, 0.0f);
        setParam (p, ParamIDs::rise, 0.0f);
        setParam (p, ParamIDs::rbPrimary, 0.0f);     // hear only the STING octave
        setParam (p, ParamIDs::linkOct1, 1.0f);
        setParam (p, ParamIDs::magicHold, 1.0f);
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 2, 220.0);
        auto out = render (p, input, sr, 256);
        double purity = 0.0;
        const double f = dominantFrequency (out, sr, 48000, purity);
        check (std::abs (1200.0 * std::log2 (f / 440.0)) < 5.0, juce::String::formatted ("hold VENOM with LINK +1: %.2f Hz (expected 440)", f));
    }

    void testDryAlignment()
    {
        std::printf ("\nSTING MIX 0%% with +1 OCT held returns the dry signal aligned with the reported latency\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Killer Bee");
        setParam (p, ParamIDs::fuzzOn, 0.0f);
        setParam (p, ParamIDs::stingMix, 0.0f);
        setParam (p, ParamIDs::oct1, 1.0f);
        const double sr = 44100.0;
        auto input = makeGuitar (sr, 44100);
        auto out = render (p, input, sr, 512);
        const double db = nullDb (out, input, p.getLatencySamples(), 8192, input.getNumSamples());
        check (db < -100.0, juce::String::formatted ("dry null %.1f dB", db));
    }

    void testCleanPathTransparency()
    {
        std::printf ("\nNothing engaged: the plug-in is transparent\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        auto out = render (p, input, sr, 256);
        const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
        check (db < -100.0, juce::String::formatted ("idle residual %.1f dB", db));
    }

    /** Power-weighted mean frequency within +-1/2 octave of 'around' (sidebands of a warbly shifter average out). */
    static double spectralCentroid (const juce::AudioBuffer<float>& b, double sr, double around, int start, int n)
    {
        double num = 0.0, den = 0.0;
        for (double f = around / 1.41; f <= around * 1.41; f += 1.0)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, c = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = start; i < start + n; ++i)
            {
                const double s0 = b.getSample (0, i) + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
            num += power * f;
            den += power;
        }
        return den > 0.0 ? num / den : 0.0;
    }

    /** Mean instantaneous frequency from Schmitt-triggered zero crossings (robust to warble / AM). */
    static double crossingFrequency (const juce::AudioBuffer<float>& b, double sr, int start, int n)
    {
        const float hyst = 0.25f * b.getMagnitude (0, start, n) * 0.2f;
        int first = -1, last = -1, count = 0;
        bool high = b.getSample (0, start) > 0.0f;
        for (int i = start + 1; i < start + n; ++i)
        {
            const float v = b.getSample (0, i);
            if (! high && v > hyst)
            {
                high = true;
                if (first < 0) first = i; else ++count;
                last = i;
            }
            else if (high && v < -hyst)
                high = false;
        }
        return count > 0 ? count * sr / (double) (last - first) : 0.0;
    }

    /** Median of per-frame autocorrelation pitch (robust to the RAW engine's crossfade nulls). */
    static double framePitch (const juce::AudioBuffer<float>& b, double sr, double expected, int start, int n)
    {
        const int frame = 4096, hop = 2048;
        const int minLag = (int) (sr / (expected * 1.5)), maxLag = (int) (sr / (expected / 1.5));
        std::vector<double> pitches;
        const float* x = b.getReadPointer (0);
        for (int f0 = start; f0 + frame + maxLag < start + n; f0 += hop)
        {
            double best = -1.0; int bestLag = 0;
            std::vector<double> r ((size_t) (maxLag + 2), 0.0);
            for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
            {
                double num = 0.0, e1 = 0.0, e2 = 0.0;
                for (int i = 0; i < frame; ++i)
                {
                    num += (double) x[f0 + i] * x[f0 + i + lag];
                    e1  += (double) x[f0 + i] * x[f0 + i];
                    e2  += (double) x[f0 + i + lag] * x[f0 + i + lag];
                }
                r[(size_t) lag] = (e1 > 0 && e2 > 0) ? num / std::sqrt (e1 * e2) : 0.0;
            }
            for (int lag = minLag; lag <= maxLag; ++lag)
                if (r[(size_t) lag] > best) { best = r[(size_t) lag]; bestLag = lag; }
            if (best < 0.5) continue;
            const double a = r[(size_t) bestLag - 1], c = r[(size_t) bestLag + 1], m = r[(size_t) bestLag];
            const double denom = a - 2.0 * m + c;
            const double shift = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
            pitches.push_back (sr / (bestLag + shift));
        }
        if (pitches.empty()) return 0.0;
        std::sort (pitches.begin(), pitches.end());
        return pitches[pitches.size() / 2];
    }

    void testNoiseOctaves()
    {
        std::printf ("\nNOISE footswitches: pitch accuracy (220 Hz sine, no Panic/Chaos/Speed)\n");
        struct Case { const char* sw; bool down; double expected; };
        const Case cases[] = { { ParamIDs::oct1, false, 440.0 }, { ParamIDs::oct2, false, 880.0 },
                               { ParamIDs::oct1, true, 110.0 },  { ParamIDs::oct2, true, 55.0 } };
        for (bool raw : { false, true })
        for (const auto& c : cases)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::stingRaw, raw ? 1.0f : 0.0f);
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::noiseDown, c.down ? 1.0f : 0.0f);
            setParam (p, c.sw, 1.0f);
            const double sr = 48000.0;
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            double purity = 0.0;
            const double f = raw ? framePitch (out, sr, c.expected, 48000, 48000) : dominantFrequency (out, sr, 48000, purity);
            const double cents = 1200.0 * std::log2 (f / c.expected);
            check (std::abs (cents) < (raw ? 20.0 : 5.0), juce::String::formatted ("%s %s %s: %.2f Hz (%+.1f cents), spurious %.1f dB",
                                                                    raw ? "RAW   " : "modern", c.sw, c.down ? "down" : "up  ", f, cents, purity));
        }
    }

    void testFootswitchRelease()
    {
        std::printf ("\nNOISE footswitch release: glides home and returns to the clean signal\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::rise, 100.0f);
        setParam (p, ParamIDs::panic, 60.0f);
        setParam (p, ParamIDs::chaos, 60.0f);
        setParam (p, ParamIDs::speed, 60.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        auto out = renderWithFootswitch (p, input, sr, 128, ParamIDs::oct1);
        // Last quarter: released for > 0.4 s -> must be back to (latency-aligned) dry
        const double db = nullDb (out, input, p.getLatencySamples(), 96000 * 3 / 4 + 24000 / 2, 96000);
        check (db < -60.0, juce::String::formatted ("after release residual %.1f dB", db));
    }

    void testRiseFall()
    {
        std::printf ("\nRISE and FALL are independent glide times\n");
        const double sr = 48000.0;
        auto semisAfter = [sr] (float riseMs, float fallMs, bool released, double seconds)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rise, riseMs);
            setParam (p, ParamIDs::fall, fallMs);
            setParam (p, ParamIDs::oct1, 1.0f);
            p.prepareToPlay (sr, 256);
            auto block = makeSine (sr, 256, 220.0);
            juce::MidiBuffer midi;
            auto run = [&] (double secs) { for (int i = 0; i < (int) (secs * sr / 256); ++i) { juce::AudioBuffer<float> b (block); p.processBlock (b, midi); } };
            run (1.2);                                   // fully risen (RISE <= 1 s)
            if (released) setParam (p, ParamIDs::oct1, 0.0f);
            run (seconds);
            return p.getMeters().pitchSemitones.load();
        };
        const float midRise = [&] { SwarmnessAudioProcessor p; resetToInit (p);
                                    setParam (p, ParamIDs::rise, 1000.0f); setParam (p, ParamIDs::oct1, 1.0f);
                                    p.prepareToPlay (sr, 256); auto block = makeSine (sr, 256, 220.0); juce::MidiBuffer midi;
                                    for (int i = 0; i < (int) (0.5 * sr / 256); ++i) { juce::AudioBuffer<float> b (block); p.processBlock (b, midi); }
                                    return p.getMeters().pitchSemitones.load(); }();
        const float slowFall = semisAfter (0.0f, 1000.0f, true, 0.5);
        const float fastFall = semisAfter (1000.0f, 0.0f, true, 0.05);
        check (midRise > 4.0f && midRise < 8.0f, juce::String::formatted ("RISE 1 s: half way after 0.5 s (%.1f st)", midRise));
        check (slowFall > 4.0f && slowFall < 8.0f, juce::String::formatted ("FALL 1 s: half way back after 0.5 s (%.1f st)", slowFall));
        check (std::abs (fastFall) < 0.5f, juce::String::formatted ("FALL 0 with RISE 1 s: home immediately (%.1f st)", fastFall));
    }

    void testRainbowInterval()
    {
        std::printf ("\nRAINBOW primary voice interval (220 Hz sine, dry removed)\n");
        for (bool raw : { false, true })
        for (float pitch : { 7.0f, -5.0f, 12.0f })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, raw ? 1.0f : 0.0f);
            setParam (p, ParamIDs::rbOn, 1.0f);
            setParam (p, ParamIDs::rbPitch, pitch);
            setParam (p, ParamIDs::rbPrimary, 100.0f);
            setParam (p, ParamIDs::rbTracking, 100.0f);
            const double sr = 48000.0;
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            // remove the dry part (rainbow adds voices to the input)
            juce::AudioBuffer<float> voices (out);
            const int lat = p.getLatencySamples();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = lat; i < voices.getNumSamples(); ++i)
                    voices.setSample (ch, i, out.getSample (ch, i) - input.getSample (ch, i - lat));
            double purity = 0.0;
            const double expected = 220.0 * std::pow (2.0, pitch / 12.0);
            const double f = raw ? framePitch (voices, sr, expected, 48000, 48000) : dominantFrequency (voices, sr, 48000, purity);
            const double cents = 1200.0 * std::log2 (f / expected);
            check (std::abs (cents) < (raw ? 20.0 : 5.0),
                   juce::String::formatted ("%s pitch %+.0f st: %.2f Hz (%+.1f cents)", raw ? "RAW   " : "modern", pitch, f, cents));
        }
    }

    void testMagicBounded()
    {
        std::printf ("\nMAGIC at maximum + switch held: self-oscillates but stays bounded\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Hive Collapse");
        setParam (p, ParamIDs::fuzzOn, 0.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000 * 6);
        for (int ch = 0; ch < 2; ++ch)
            input.clear (ch, 48000 * 2, 48000 * 4);   // silence: whatever sounds now is self-oscillation
        auto out = renderWithFootswitch (p, input, sr, 256, nullptr, true);
        const float peak = out.getMagnitude (0, out.getNumSamples());
        const float tail = out.getRMSLevel (0, 48000 * 4, 48000);
        check (allFinite (out) && peak <= 2.0f && tail > 0.01f, juce::String::formatted ("peak %.2f, self-oscillation during silence %.1f dBFS", peak,
                                                                          juce::Decibels::gainToDecibels (tail)));
    }

    void testFuzzLevel()
    {
        std::printf ("\nFUZZ: loud but controlled output level\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        const double inRms = juce::Decibels::gainToDecibels ((double) input.getRMSLevel (0, 24000, 72000));
        const char* voices[] = { "DOWN", "MID", "UP" };
        for (int voice = 0; voice < 3; ++voice)
            for (float fz : { 0.0f, 50.0f, 100.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::fuzzOn, 1.0f);
                setParam (p, ParamIDs::fuzzVoice, (float) voice);
                setParam (p, ParamIDs::fuzz, fz);
                auto out = render (p, input, sr, 256);
                const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
                check (rms - inRms > -3.0 && rms - inRms < 14.0,
                       juce::String::formatted ("%-4s fuzz %3.0f%%: %+.1f dB vs input", voices[voice], fz, rms - inRms));
            }

        for (auto [id, name] : { std::pair { ParamIDs::fuzzGlare, "GLARE" }, std::pair { ParamIDs::fuzzBlend, "BLEND" },
                                 std::pair { ParamIDs::fuzzScoop, "SCOOP" } })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 100.0f);
            setParam (p, ParamIDs::fuzzTone, 100.0f);
            setParam (p, id, 100.0f);
            auto out = render (p, input, sr, 256);
            const float peak = out.getMagnitude (0, out.getNumSamples());
            const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
            check (peak < 2.0f && rms - inRms < 16.0, juce::String::formatted ("%s max: %+.1f dB vs input, peak %.2f", name, rms - inRms, peak));
        }
    }

    static double goertzelDb (const juce::AudioBuffer<float>& b, double sr, double freq, int start, int n)
    {
        const double w = juce::MathConstants<double>::twoPi * freq / sr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = start; i < start + n; ++i)
        {
            const double s0 = b.getSample (0, i) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return juce::Decibels::gainToDecibels (std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / n + 1.0e-12);
    }

    void testGlareOctave()
    {
        std::printf ("\nSMOKE GLARE adds an octave-up\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000, 110.0, 0.3f);
        double second[2] {};
        for (int g = 0; g < 2; ++g)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 60.0f);
            setParam (p, ParamIDs::fuzzGlare, g == 0 ? 0.0f : 100.0f);
            auto out = render (p, input, sr, 256);
            second[g] = goertzelDb (out, sr, 220.0, 12000, 24000) - goertzelDb (out, sr, 110.0, 12000, 24000);
        }
        check (second[1] > second[0] + 6.0,
               juce::String::formatted ("2nd harmonic vs fundamental: %.1f dB -> %.1f dB with GLARE", second[0], second[1]));
    }

    void testTrails()
    {
        std::printf ("\nHIVE TRAILS: repeats at TIME, decay on their own, no self-oscillation without VENOM\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 5, 220.0, 0.3f);
        for (int ch = 0; ch < 2; ++ch)
            input.clear (ch, 4800, input.getNumSamples() - 4800);    // 100 ms burst, then silence

        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::rbOn, 1.0f);
        setParam (p, ParamIDs::rbPitch, 7.0f);
        setParam (p, ParamIDs::rbPrimary, 100.0f);
        setParam (p, ParamIDs::rbTracking, 100.0f);
        setParam (p, ParamIDs::rbMagic, 100.0f);
        setParam (p, ParamIDs::rbTime, 300.0f);
        auto out = render (p, input, sr, 256);

        // Energy between repeats vs. at the first repeat (~300 ms after the burst)
        const float atRepeat  = out.getRMSLevel (0, (int) (0.30 * sr), (int) (0.08 * sr));
        const float between   = out.getRMSLevel (0, (int) (0.19 * sr), (int) (0.06 * sr));
        const float tail      = out.getRMSLevel (0, (int) (4.0 * sr), (int) (0.8 * sr));
        check (atRepeat > 3.0f * between, juce::String::formatted ("first repeat at TIME: %.1f dB above the gap",
                                                                   juce::Decibels::gainToDecibels (atRepeat / (between + 1.0e-9f))));
        check (tail < 0.003f, juce::String::formatted ("TRAILS 100%% has faded after 4 s: tail %.1f dBFS", juce::Decibels::gainToDecibels (tail + 1.0e-9f)));
    }

    void testInputSensitivity()
    {
        std::printf ("\nINPUT: changes how hard the effects are hit, transparent otherwise\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::input, 12.0f);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
            check (db < -100.0, juce::String::formatted ("INPUT +12 dB, nothing engaged: residual %.1f dB", db));
        }
        double rms[2] {};
        for (int k = 0; k < 2; ++k)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 20.0f);
            setParam (p, ParamIDs::input, k == 0 ? -18.0f : 12.0f);
            auto out = render (p, input, sr, 256);
            rms[k] = out.getRMSLevel (0, 12000, 24000);
        }
        check (rms[0] > rms[1] * 1.5, "SMOKE at low INPUT cleans up relative to high INPUT (output compensated)");
    }

    void testFuzzIdleNoise()
    {
        std::printf ("\nSMOKE with nothing played: no self-oscillation, interface hiss not blown up\n");
        const double sr = 48000.0;
        for (float noiseDb : { -200.0f, -90.0f, -75.0f, -68.0f })
        {
            juce::AudioBuffer<float> input (2, 48000 * 2);
            juce::Random rng (7);
            const float amp = juce::Decibels::decibelsToGain (noiseDb, -200.0f);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < input.getNumSamples(); ++i)
                    input.setSample (ch, i, amp * (rng.nextFloat() * 2.0f - 1.0f));

            for (const char* preset : { "Swollen Smoke", "Glare Scream", "Doom Cathedral", "Smoked Out", "Hive Collapse", "Queen Scream" })
            {
                SwarmnessAudioProcessor p;
                p.getPresetManager().loadPreset (preset);
                auto out = render (p, input, sr, 256);
                const float rms = out.getRMSLevel (0, 48000, 48000);
                const float db = juce::Decibels::gainToDecibels (rms, -200.0f);
                check (db < noiseDb - 3.0f || db < -100.0f,
                       juce::String::formatted ("%-16s input noise %4.0f dBFS -> output %6.1f dBFS", preset, noiseDb, db));
            }
        }
    }

    /** Time (ms) until the output first reaches half of its steady-state level after a sine starts. */
    static double onsetMs (const juce::AudioBuffer<float>& out, double sr, int startSample)
    {
        const int n = out.getNumSamples();
        const float steady = out.getRMSLevel (0, n - (int) (0.3 * sr), (int) (0.25 * sr));
        const int win = (int) (0.004 * sr);
        for (int i = startSample; i + win < n; i += win / 2)
            if (out.getRMSLevel (0, i, win) > 0.5f * steady)
                return 1000.0 * (i - startSample) / sr;
        return -1.0;
    }

    void reportLag()
    {
        std::printf ("\nPitch engines: onset lag vs. the dry note (220 Hz sine starting at 0.5 s)\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 2, 220.0, 0.3f);
        for (int ch = 0; ch < 2; ++ch)
            input.clear (ch, 0, 24000);
        for (bool raw : { false, true })
        {
            for (const char* sw : { ParamIDs::oct1, ParamIDs::oct2 })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::stingRaw, raw ? 1.0f : 0.0f);
                setParam (p, ParamIDs::rise, 0.0f);
                setParam (p, sw, 1.0f);
                auto out = render (p, input, sr, 256);
                std::printf ("    STING %s %s: %.1f ms\n", raw ? "RAW   " : "modern", sw, onsetMs (out, sr, 24000 + p.getLatencySamples()));
            }
            for (float tracking : { 100.0f, 80.0f, 30.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::rbRaw, raw ? 1.0f : 0.0f);
                setParam (p, ParamIDs::rbOn, 1.0f);
                setParam (p, ParamIDs::rbPitch, 7.0f);
                setParam (p, ParamIDs::rbPrimary, 100.0f);
                setParam (p, ParamIDs::rbTracking, tracking);
                auto out = render (p, input, sr, 256);
                const int lat = p.getLatencySamples();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = lat; i < out.getNumSamples(); ++i)
                        out.setSample (ch, i, out.getSample (ch, i) - input.getSample (ch, i - lat));
                std::printf ("    HIVE  %s tracking %3.0f%%: %.1f ms\n", raw ? "RAW   " : "modern", tracking, onsetMs (out, sr, 24000 + lat));
            }
        }
    }

    void testSwarmBounded()
    {
        std::printf ("\nSWARM: extreme settings stay bounded\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        const double inRms = juce::Decibels::gainToDecibels ((double) input.getRMSLevel (0, 24000, 72000));
        for (bool deep : { false, true })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::swarmOn, 1.0f);
            setParam (p, ParamIDs::swarmDeep, deep ? 1.0f : 0.0f);
            setParam (p, ParamIDs::swarmDepth, 100.0f);
            setParam (p, ParamIDs::swarmRate, 8.0f);
            setParam (p, ParamIDs::swarmMix, 50.0f);
            auto out = render (p, input, sr, 256);
            const float peak = out.getMagnitude (0, out.getNumSamples());
            const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
            check (peak < 1.5f && std::abs (rms - inRms) < 9.0,
                   juce::String::formatted ("%s depth 100%%, mix 50%%: %+.1f dB vs input, peak %.2f", deep ? "deep   " : "classic", rms - inRms, peak));
        }
    }

    void testStateRoundTrip()
    {
        std::printf ("\nState save / restore\n");
        SwarmnessAudioProcessor a;
        a.getPresetManager().loadPreset ("Honey Trails");
        setParam (a, ParamIDs::fuzz, 42.0f);
        setParam (a, ParamIDs::oct1, 1.0f);   // momentary switch left down
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        SwarmnessAudioProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto value = [&b] (const char* id) { return b.getAPVTS().getRawParameterValue (id)->load(); };
        check (std::abs (value (ParamIDs::fuzz) - 42.0f) < 0.05f && std::abs (value (ParamIDs::rbMagic) - 55.0f) < 0.05f,
               "parameters restored");
        check (value (ParamIDs::oct1) < 0.5f, "momentary footswitch not restored as held");
        check (b.getPresetManager().getCurrentPresetName() == "Honey Trails", "preset name restored");
    }

    void testPresetDirtyTracking()
    {
        std::printf ("\nPreset dirty tracking\n");
        SwarmnessAudioProcessor p;
        auto& pm = p.getPresetManager();
        pm.loadPreset ("Frenzy");
        check (! pm.isDirty(), "clean after load");
        setParam (p, ParamIDs::fuzzBlend, 12.0f);
        check (pm.isDirty(), "dirty after edit");
        pm.loadPreset ("Frenzy");
        setParam (p, ParamIDs::oct2, 1.0f);
        setParam (p, ParamIDs::bypass, 1.0f);
        check (! pm.isDirty(), "footswitches / bypass do not affect preset state");
    }

    void testPresetBanks()
    {
        std::printf ("\nPreset banks (factory / user)\n");
        SwarmnessAudioProcessor p;
        auto& pm = p.getPresetManager();
        const auto factory = pm.getFactoryPresetNames();

        pm.loadPreset (factory[0]);
        pm.loadNextPreset (false);
        check (pm.getCurrentPresetName() == factory[1], "factory: next steps within the factory bank");
        pm.loadPreset (factory[0]);
        pm.loadPreviousPreset (false);
        check (pm.getCurrentPresetName() == factory[factory.size() - 1], "factory: previous wraps to the last factory preset");

        const juce::String a ("zz bank test A"), b ("zz bank test B");
        pm.saveUserPreset (a);
        pm.saveUserPreset (b);
        const auto user = pm.getUserPresetNames();
        bool noFactory = true;
        for (const auto& n : user)
            noFactory = noFactory && ! factory.contains (n);
        check (noFactory && user.contains (a) && user.contains (b), "user bank lists only user presets");

        pm.loadPreset (factory[factory.size() - 1]);
        pm.loadNextPreset (true);
        check (pm.getCurrentPresetName() == user[0], "user: from a factory preset, next lands on the first user preset");
        pm.loadPreset (b);
        pm.loadNextPreset (false);
        check (pm.getCurrentPresetName() == factory[0], "factory: from a user preset, next lands on the first factory preset");

        pm.loadPreset (b);
        pm.deleteUserPreset (b);
        check (pm.isUserPreset (pm.getCurrentPresetName()), "deleting a user preset moves to a neighbouring user preset");
        pm.deleteUserPreset (a);
    }

    void testPerformance()
    {
        std::printf ("\nPerformance (48 kHz, 128-sample blocks)\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Hive Collapse");
        setParam (p, ParamIDs::panic, 80.0f);
        setParam (p, ParamIDs::chaos, 50.0f);
        setParam (p, ParamIDs::speed, 50.0f);
        setParam (p, ParamIDs::rbSecondary, 50.0f);
        setParam (p, ParamIDs::swarmOn, 1.0f);
        setParam (p, ParamIDs::swarmDeep, 1.0f);
        setParam (p, ParamIDs::flowOn, 1.0f);
        setParam (p, ParamIDs::oct1, 1.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, (int) sr * 10);
        const auto t0 = std::chrono::steady_clock::now();
        auto out = render (p, input, sr, 128);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double load = secs / 10.0 * 100.0;
        check (load < 25.0, juce::String::formatted ("everything on: %.2f%% of one core (realtime factor %.0fx)", load, 10.0 / secs));
    }

    void renderPresets (const juce::File& dir)
    {
        dir.createDirectory();
        const double sr = 48000.0;
        auto input = makeGuitar (sr, (int) sr * 4);
        SwarmnessAudioProcessor p;
        for (const auto& name : p.getPresetManager().getFactoryPresetNames())
        {
            p.getPresetManager().loadPreset (name);
            setParam (p, ParamIDs::flowSync, 0.0f);
            auto out = renderWithFootswitch (p, input, sr, 256, ParamIDs::oct1, true);
            auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            if (stream != nullptr)
                if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                                            .withNumChannels (2)
                                                                                            .withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            std::printf ("  rendered %s\n", file.getFullPathName().toRawUTF8());
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String (argv[1]) == "--screenshot")
    {
        // Renders the editor to PNG (useful for design review / docs).
        SwarmnessAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        if (argc >= 4)
            p.getPresetManager().loadPreset (argv[3]);
        p.getAPVTS().getParameter (ParamIDs::oct1)->setValueNotifyingHost (1.0f);
        const float scale = argc >= 5 ? juce::String (argv[4]).getFloatValue() : 1.0f;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (juce::roundToInt (1000 * scale), juce::roundToInt (680 * scale));

        // Feed a little audio so meters and the pitch trace show activity.
        auto input = makeGuitar (48000.0, 256);
        juce::MidiBuffer midi;
        for (int i = 0; i < 120; ++i)
        {
            juce::AudioBuffer<float> b (input);
            p.processBlock (b, midi);
            if (i % 4 == 0)
                if (auto* ed = dynamic_cast<SwarmnessAudioProcessorEditor*> (editor.get()))
                    ed->refresh();
        }

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--render")
    {
        renderPresets (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        return 0;
    }

    std::printf ("Swarmness DSP tests\n");
    testPresetsStable();
    testBypassNull();
    testFootswitchesOverrideBypass();
    testVenomLink();
    testDryAlignment();
    testCleanPathTransparency();
    testNoiseOctaves();
    testFootswitchRelease();
    testRiseFall();
    testRainbowInterval();
    testMagicBounded();
    testFuzzLevel();
    testGlareOctave();
    testTrails();
    reportLag();
    testFuzzIdleNoise();
    testInputSensitivity();
    testSwarmBounded();
    testStateRoundTrip();
    testPresetDirtyTracking();
    testPresetBanks();
    testPerformance();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
