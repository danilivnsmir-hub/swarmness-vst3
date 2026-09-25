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
        p.getPresetManager().loadPreset ("Self Destruct");
        setParam (p, ParamIDs::bypass, 1.0f);
        setParam (p, ParamIDs::oct1, 1.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        auto out = render (p, input, sr, 256);
        const int latency = p.getLatencySamples();
        const double db = nullDb (out, input, latency, 4096, input.getNumSamples());
        check (db < -120.0, juce::String::formatted ("bypass null %.1f dB (latency %d samples)", db, latency));
    }

    void testDryAlignment()
    {
        std::printf ("\nMix 0%% returns the dry signal aligned with the reported latency\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Whale Song");
        setParam (p, ParamIDs::mix, 0.0f);
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

    void testNoiseOctaves()
    {
        std::printf ("\nNOISE footswitches: pitch accuracy (220 Hz sine, no Panic/Chaos/Speed)\n");
        struct Case { const char* sw; bool down; double expected; };
        const Case cases[] = { { ParamIDs::oct1, false, 440.0 }, { ParamIDs::oct2, false, 880.0 },
                               { ParamIDs::oct1, true, 110.0 },  { ParamIDs::oct2, true, 55.0 } };
        for (const auto& c : cases)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::noiseDown, c.down ? 1.0f : 0.0f);
            setParam (p, c.sw, 1.0f);
            const double sr = 48000.0;
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            double purity = 0.0;
            const double f = dominantFrequency (out, sr, 48000, purity);
            const double cents = 1200.0 * std::log2 (f / c.expected);
            check (std::abs (cents) < 5.0, juce::String::formatted ("%s %s: %.2f Hz (%+.1f cents), spurious %.1f dB",
                                                                    c.sw, c.down ? "down" : "up  ", f, cents, purity));
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

    void testRainbowInterval()
    {
        std::printf ("\nRAINBOW primary voice interval (220 Hz sine, dry removed)\n");
        for (float pitch : { 7.0f, -5.0f, 12.0f })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
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
            const double f = dominantFrequency (voices, sr, 48000, purity);
            const double expected = 220.0 * std::pow (2.0, pitch / 12.0);
            const double cents = 1200.0 * std::log2 (f / expected);
            check (std::abs (cents) < 5.0, juce::String::formatted ("pitch %+.0f st: %.2f Hz (%+.1f cents)", pitch, f, cents));
        }
    }

    void testMagicBounded()
    {
        std::printf ("\nMAGIC at maximum + switch held: self-oscillates but stays bounded\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Self Destruct");
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
        for (float fz : { 0.0f, 50.0f, 100.0f })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, fz);
            auto out = render (p, input, sr, 256);
            const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
            check (rms - inRms > -6.0 && rms - inRms < 14.0, juce::String::formatted ("fuzz %3.0f%%: %+.1f dB vs input", fz, rms - inRms));
        }
    }

    void testStateRoundTrip()
    {
        std::printf ("\nState save / restore\n");
        SwarmnessAudioProcessor a;
        a.getPresetManager().loadPreset ("Pixie Trails");
        setParam (a, ParamIDs::fuzz, 42.0f);
        setParam (a, ParamIDs::oct1, 1.0f);   // momentary switch left down
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        SwarmnessAudioProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto value = [&b] (const char* id) { return b.getAPVTS().getRawParameterValue (id)->load(); };
        check (std::abs (value (ParamIDs::fuzz) - 42.0f) < 0.05f && std::abs (value (ParamIDs::rbMagic) - 45.0f) < 0.05f,
               "parameters restored");
        check (value (ParamIDs::oct1) < 0.5f, "momentary footswitch not restored as held");
        check (b.getPresetManager().getCurrentPresetName() == "Pixie Trails", "preset name restored");
    }

    void testPresetDirtyTracking()
    {
        std::printf ("\nPreset dirty tracking\n");
        SwarmnessAudioProcessor p;
        auto& pm = p.getPresetManager();
        pm.loadPreset ("Chaos Engine");
        check (! pm.isDirty(), "clean after load");
        setParam (p, ParamIDs::mix, 12.0f);
        check (pm.isDirty(), "dirty after edit");
        pm.loadPreset ("Chaos Engine");
        setParam (p, ParamIDs::oct2, 1.0f);
        setParam (p, ParamIDs::bypass, 1.0f);
        check (! pm.isDirty(), "footswitches / bypass do not affect preset state");
    }

    void testPerformance()
    {
        std::printf ("\nPerformance (48 kHz, 128-sample blocks)\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Self Destruct");
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
    testDryAlignment();
    testCleanPathTransparency();
    testNoiseOctaves();
    testFootswitchRelease();
    testRainbowInterval();
    testMagicBounded();
    testFuzzLevel();
    testStateRoundTrip();
    testPresetDirtyTracking();
    testPerformance();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
