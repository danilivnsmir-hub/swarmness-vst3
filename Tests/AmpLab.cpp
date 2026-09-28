// SwarmnessAmpLab - measures amps the same way, whatever they are: a NAM capture, one of our AMP
// models (or a single reference circuit), or WASP. Used to compare our circuits against captures of
// real amps and to fit the circuit values to them. Captures are only measured, never shipped.
//
//   SwarmnessAmpLab features <target>          -> JSON feature set on stdout
//   SwarmnessAmpLab render <target> <in.wav> <out.wav>
//
// <target>:
//   nam:<file.nam>
//   amp:<channel 0..2>:<character 0..1>[:gain:bass:mid:treble:presence:depth:master]   (knobs 0..10)
//   wasp:<drive>:<bright>:<attack>:<volume>                                             (knobs 0..10)
//   any target can end in @<dB> = input gain in front of it

#include <JuceHeader.h>
#include "DSP/AmpBlock.h"
#include "DSP/DriveBlock.h"
#include "DSP/CabBlock.h"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;

/** A fresh instance of the device under test: process (in, out, n) at 48 kHz, mono. */
using Device = std::function<void (const float*, float*, int)>;
using Factory = std::function<Device()>;

Factory parseTarget (juce::String t)
{
    float inGain = 1.0f;
    if (t.contains ("@"))
    {
        inGain = juce::Decibels::decibelsToGain (t.fromLastOccurrenceOf ("@", false, false).getFloatValue());
        t = t.upToLastOccurrenceOf ("@", false, false);
    }
    auto withGain = [inGain] (Device d) -> Device
    {
        if (inGain == 1.0f) return d;
        auto buf = std::make_shared<std::vector<float>>();
        return [d, inGain, buf] (const float* in, float* out, int n)
        {
            buf->resize ((size_t) n);
            for (int i = 0; i < n; ++i) (*buf)[(size_t) i] = in[i] * inGain;
            d (buf->data(), out, n);
        };
    };

    const auto kind = t.upToFirstOccurrenceOf (":", false, false);
    const auto rest = t.fromFirstOccurrenceOf (":", false, false);
    if (kind == "nam")
    {
        const auto json = std::make_shared<std::string> (juce::File (rest).loadFileAsString().toStdString());
        return [json, withGain]() -> Device
        {
            std::string err;
            std::shared_ptr<NamModel> m (NamModel::load (*json, err).release());
            if (m == nullptr) { std::fprintf (stderr, "NAM load failed: %s\n", err.c_str()); std::exit (2); }
            if (std::abs (m->getSampleRate() - kRate) > 1.0) { std::fprintf (stderr, "capture is not 48 kHz\n"); std::exit (2); }
            m->reset (4096);
            return withGain ([m] (const float* in, float* out, int n) { m->process (in, out, n); });
        };
    }
    auto nums = juce::StringArray::fromTokens (rest, ":", "");
    auto num = [&nums] (int i, float def) { return i < nums.size() ? nums[i].getFloatValue() : def; };
    if (kind == "amp")
    {
        AmpBlock::Settings s;
        s.on = true;
        s.channel = (int) num (0, 1);
        s.character = num (1, 0.0f);
        s.gain = num (2, 5) * 0.1f; s.bass = num (3, 5) * 0.1f; s.mid = num (4, 5) * 0.1f; s.treble = num (5, 5) * 0.1f;
        s.presence = num (6, 5) * 0.1f; s.depth = num (7, 5) * 0.1f; s.master = num (8, 5) * 0.1f;
        return [s, withGain]() -> Device
        {
            auto a = std::make_shared<AmpBlock>();
            a->prepare (kRate, 512);
            a->setParams (s);
            a->reset();
            auto stereo = std::make_shared<juce::AudioBuffer<float>> (2, 512);
            return withGain ([a, stereo] (const float* in, float* out, int n)
            {
                for (int start = 0; start < n; start += 512)
                {
                    const int m = juce::jmin (512, n - start);
                    stereo->copyFrom (0, 0, in + start, m);
                    stereo->copyFrom (1, 0, in + start, m);
                    float* p[2] { stereo->getWritePointer (0), stereo->getWritePointer (1) };
                    a->process (p, 2, m);
                    std::memcpy (out + start, p[0], sizeof (float) * (size_t) m);
                }
            });
        };
    }
    if (kind == "wasp")
    {
        DriveBlock::Settings s;
        s.on = true;
        s.drive = num (0, 3) * 0.1f; s.bright = num (1, 5) * 0.1f; s.attack = num (2, 5) * 0.1f; s.volume = num (3, 5) * 0.1f;
        return [s, withGain]() -> Device
        {
            auto d = std::make_shared<DriveBlock>();
            d->prepare (kRate, 512);
            d->setParams (s);
            d->reset();
            auto stereo = std::make_shared<juce::AudioBuffer<float>> (2, 512);
            return withGain ([d, stereo] (const float* in, float* out, int n)
            {
                for (int start = 0; start < n; start += 512)
                {
                    const int m = juce::jmin (512, n - start);
                    stereo->copyFrom (0, 0, in + start, m);
                    stereo->copyFrom (1, 0, in + start, m);
                    float* p[2] { stereo->getWritePointer (0), stereo->getWritePointer (1) };
                    d->process (p, 2, m);
                    std::memcpy (out + start, p[0], sizeof (float) * (size_t) m);
                }
            });
        };
    }
    std::fprintf (stderr, "unknown target %s\n", t.toRawUTF8());
    std::exit (2);
}

std::vector<float> run (const Factory& f, const std::vector<float>& in)
{
    auto d = f();
    std::vector<float> out (in.size());
    for (size_t s = 0; s < in.size(); s += 4096)
    {
        const int n = (int) std::min<size_t> (4096, in.size() - s);
        d (in.data() + s, out.data() + s, n);
    }
    return out;
}

//==============================================================================
constexpr int kOrder = 15, kN = 1 << kOrder;   // 32768 points, 1.46 Hz bins
constexpr double kDf = kRate / kN;

/** Complex spectrum of the last period (rectangular window: the test signals are periodic in kN). */
std::vector<std::complex<float>> spectrum (const std::vector<float>& x, size_t start)
{
    juce::dsp::FFT fft (kOrder);
    std::vector<std::complex<float>> in ((size_t) kN), out ((size_t) kN);
    for (int i = 0; i < kN; ++i) in[(size_t) i] = x[start + (size_t) i];
    fft.perform (in.data(), out.data(), false);
    return out;
}

struct Multitone
{
    std::vector<int> bins;
    std::vector<float> signal;   // one period
};

Multitone makeMultitone()
{
    Multitone m;
    std::mt19937 rng (7);
    std::uniform_real_distribution<double> ph (0.0, juce::MathConstants<double>::twoPi);
    int last = 0;
    for (double f = 40.0; f < 12500.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        int k = (int) std::round (f / kDf) | 1;   // odd bins: 2nd-order products land on even bins
        if (k <= last) k = last + 2;
        m.bins.push_back (k);
        last = k;
    }
    m.signal.assign ((size_t) kN, 0.0f);
    std::vector<double> acc ((size_t) kN, 0.0);
    for (int k : m.bins)
    {
        const double p = ph (rng);
        for (int i = 0; i < kN; ++i)
            acc[(size_t) i] += std::sin (juce::MathConstants<double>::twoPi * k * i / kN + p);
    }
    double rms = 0.0;
    for (double v : acc) rms += v * v;
    rms = std::sqrt (rms / kN);
    for (int i = 0; i < kN; ++i) m.signal[(size_t) i] = (float) (acc[(size_t) i] / rms);   // unit RMS
    return m;
}

/** Periodic signal repeated `periods` times at a level. */
std::vector<float> repeat (const std::vector<float>& one, int periods, float gain)
{
    std::vector<float> x (one.size() * (size_t) periods);
    for (size_t i = 0; i < x.size(); ++i) x[i] = one[i % one.size()] * gain;
    return x;
}

double db (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

struct Json
{
    std::string s = "{";
    bool first = true;
    void key (const std::string& k) { s += (first ? "\n\"" : ",\n\"") + k + "\": "; first = false; }
    void num (const std::string& k, double v) { key (k); s += juce::String (v, 3).toStdString(); }
    void arr (const std::string& k, const std::vector<double>& v)
    {
        key (k);
        s += "[";
        for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + juce::String (v[i], 3).toStdString();
        s += "]";
    }
    std::string done() { return s + "\n}\n"; }
};

/** A plucked DI-like riff: palm-muted low chugs, open chords, single notes (Karplus-Strong). */
std::vector<float> guitarRiff (double seconds)
{
    const int n = (int) (seconds * kRate);
    std::vector<float> out ((size_t) n, 0.0f);
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    auto pluck = [&] (int at, double hz, float amp, double decay, float bright)
    {
        const int len = (int) std::round (kRate / hz);
        std::vector<float> line ((size_t) len);
        float lp = 0.0f;
        for (auto& v : line) { lp += bright * (uni (rng) - lp); v = lp * amp; }
        float prev = 0.0f;
        const float fb = (float) std::exp (-1.0 / (decay * hz));
        for (int i = at, k = 0; i < n && i < at + (int) (kRate * 2.5); ++i, ++k)
        {
            const size_t idx = (size_t) (k % len);
            const float v = line[idx];
            line[idx] = fb * 0.5f * (v + prev);
            prev = v;
            out[(size_t) i] += v;
        }
    };
    const double E = 82.41, A = 110.0, D = 146.83, G = 196.0;
    double t = 0.0;
    int bar = 0;
    while (t < seconds - 2.5)
    {
        const int at = (int) (t * kRate);
        if (bar % 4 < 2)
        {
            for (int c = 0; c < 8; ++c)   // chugs: E5, short
            {
                pluck (at + (int) (c * 0.125 * kRate), E, 0.35f, 0.08, 0.5f);
                pluck (at + (int) (c * 0.125 * kRate) + 40, E * 1.5, 0.25f, 0.08, 0.5f);
            }
            t += 1.0;
        }
        else if (bar % 4 == 2)
        {
            for (double f : { A, A * 1.5, A * 2.0, A * 2.52, A * 3.0 })   // open chord, strummed
                pluck (at + (int) (f * 3.0), f, 0.22f, 1.2, 0.8f);
            t += 1.5;
        }
        else
        {
            const double notes[] { D * 2, G * 2, A * 2, D * 3 };
            for (int k = 0; k < 4; ++k)
                pluck (at + (int) (k * 0.25 * kRate), notes[k], 0.3f, 0.7, 0.7f);
            t += 1.0;
        }
        ++bar;
    }
    // unit peak
    float peak = 0.0f;
    for (float v : out) peak = std::max (peak, std::abs (v));
    for (auto& v : out) v /= peak;
    return out;
}

//==============================================================================
std::string features (const Factory& f)
{
    Json j;
    const auto mt = makeMultitone();
    std::vector<double> freqs;
    for (int k : mt.bins) freqs.push_back (k * kDf);
    j.arr ("mt_freqs", freqs);

    // A/B: multitone response at several RMS levels (small signal .. hard playing)
    for (double lvl : { -80.0, -66.0, -52.0, -40.0, -28.0, -18.0 })
    {
        const float g = (float) juce::Decibels::decibelsToGain (lvl);
        auto x = repeat (mt.signal, 4, g);
        auto y = run (f, x);
        auto X = spectrum (x, (size_t) kN * 3), Y = spectrum (y, (size_t) kN * 3);
        std::vector<double> resp;
        double toneE = 0.0, allE = 0.0;
        std::vector<bool> isTone ((size_t) kN / 2, false);
        for (int k : mt.bins)
        {
            resp.push_back (db (std::abs (Y[(size_t) k]) / std::abs (X[(size_t) k])));
            toneE += std::norm (Y[(size_t) k]);
            isTone[(size_t) k] = true;
        }
        for (int k = 1; k < kN / 2; ++k)
            if (kDf * k > 30.0 && kDf * k < 16000.0) allE += std::norm (Y[(size_t) k]);
        const auto tag = juce::String ((int) -lvl).toStdString();
        j.arr ("mt_resp_" + tag, resp);
        j.num ("mt_imd_" + tag, 10.0 * std::log10 (std::max (1.0e-30, (allE - toneE)) / std::max (1.0e-30, toneE)));
        double outRms = 0.0;
        for (int i = kN * 3; i < kN * 4; ++i) outRms += (double) y[(size_t) i] * y[(size_t) i];
        j.num ("mt_out_" + tag, 10.0 * std::log10 (outRms / kN + 1.0e-30));
    }

    // C: sines, fundamental level and harmonics vs input level
    for (double f0 : { 110.0, 440.0 })
    {
        const int k0 = (int) std::round (f0 / kDf);
        std::vector<float> one ((size_t) kN);
        for (int i = 0; i < kN; ++i) one[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * k0 * i / kN);
        std::vector<double> fund, h2, h3, h4, h5, thd, hiRatio;
        for (double lvl : { -72.0, -62.0, -52.0, -42.0, -32.0, -22.0, -12.0, -2.0 })
        {
            auto x = repeat (one, 3, (float) juce::Decibels::decibelsToGain (lvl));
            auto y = run (f, x);
            auto Y = spectrum (y, (size_t) kN * 2);
            const double a1 = std::abs (Y[(size_t) k0]) * 2.0 / kN;
            fund.push_back (db (a1) - lvl);
            auto h = [&] (int n) { return db (std::abs (Y[(size_t) (k0 * n)]) * 2.0 / kN) - db (a1); };
            h2.push_back (h (2)); h3.push_back (h (3)); h4.push_back (h (4)); h5.push_back (h (5));
            double e = 0.0, hi = 0.0;
            for (int n = 2; k0 * n < kN / 2 && n * f0 < 20000.0; ++n)
            {
                const double p = std::norm (Y[(size_t) (k0 * n)]);
                e += p;
                if (n * f0 > 3000.0) hi += p;
            }
            thd.push_back (10.0 * std::log10 (e / std::norm (Y[(size_t) k0]) + 1.0e-30));
            hiRatio.push_back (10.0 * std::log10 (hi / (e + 1.0e-30) + 1.0e-30));
        }
        const auto tag = juce::String ((int) f0).toStdString();
        j.arr ("sine_gain_" + tag, fund);
        j.arr ("sine_h2_" + tag, h2); j.arr ("sine_h3_" + tag, h3); j.arr ("sine_h4_" + tag, h4); j.arr ("sine_h5_" + tag, h5);
        j.arr ("sine_thd_" + tag, thd);
        j.arr ("sine_hifizz_" + tag, hiRatio);
    }

    // D: dynamics - silence, a 196 Hz burst at -14 dBFS, then a small 1 kHz probe (bias / sag recovery)
    {
        const int n = (int) (kRate * 1.2);
        std::vector<float> x ((size_t) n, 0.0f);
        const int b0 = (int) (0.2 * kRate), b1 = (int) (0.5 * kRate);
        const float a = (float) juce::Decibels::decibelsToGain (-14.0), p = (float) juce::Decibels::decibelsToGain (-40.0);
        for (int i = b0; i < b1; ++i) x[(size_t) i] = a * (float) std::sin (juce::MathConstants<double>::twoPi * 196.0 * (i - b0) / kRate);
        for (int i = b1; i < n; ++i) x[(size_t) i] = p * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / kRate);
        auto y = run (f, x);
        std::vector<double> env;
        const int w = (int) (0.01 * kRate);
        for (int s = b0; s + w <= n; s += w)
        {
            double e = 0.0;
            for (int i = s; i < s + w; ++i) e += (double) y[(size_t) i] * y[(size_t) i];
            env.push_back (10.0 * std::log10 (e / w + 1.0e-30));
        }
        j.arr ("burst_env", env);
    }

    // E: the riff - long-term spectrum (1/3 octave) at three input levels, and the crest factor
    {
        const auto riff = guitarRiff (8.0);
        std::vector<double> centres;
        for (double fc = 50.0; fc < 16000.0; fc *= std::pow (2.0, 1.0 / 3.0)) centres.push_back (fc);
        j.arr ("ltas_freqs", centres);
        for (double peakDb : { -36.0, -24.0, -12.0 })
        {
            std::vector<float> x (riff);
            const float g = (float) juce::Decibels::decibelsToGain (peakDb);
            for (auto& v : x) v *= g;
            auto y = run (f, x);
            constexpr int order = 13, N = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<double> power ((size_t) N / 2, 0.0);
            std::vector<float> buf ((size_t) N * 2);
            int frames = 0;
            for (size_t s = (size_t) kRate / 2; s + N <= y.size(); s += N / 2, ++frames)
            {
                for (int i = 0; i < N; ++i)
                    buf[(size_t) i] = y[s + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N));
                std::fill (buf.begin() + N, buf.end(), 0.0f);
                fft.performFrequencyOnlyForwardTransform (buf.data());
                for (int k = 0; k < N / 2; ++k) power[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
            }
            std::vector<double> bands;
            double total = 0.0;
            for (double fc : centres)
            {
                const double lo = fc / std::pow (2.0, 1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
                double e = 0.0;
                for (int k = (int) std::ceil (lo / (kRate / N)); k <= (int) (hi / (kRate / N)) && k < N / 2; ++k) e += power[(size_t) k];
                bands.push_back (e);
                total += e;
            }
            for (auto& b : bands) b = 10.0 * std::log10 (b / total + 1.0e-30);
            const auto tag = juce::String ((int) -peakDb).toStdString();
            j.arr ("ltas_" + tag, bands);
            double rms = 0.0, pk = 0.0;
            for (size_t i = (size_t) kRate / 2; i < y.size(); ++i) { rms += (double) y[i] * y[i]; pk = std::max (pk, (double) std::abs (y[i])); }
            rms = std::sqrt (rms / (double) (y.size() - (size_t) kRate / 2));
            j.num ("riff_crest_" + tag, db (pk / rms));
            j.num ("riff_rms_" + tag, db (rms));
        }
    }
    return j.done();
}

/** A reduced, faster feature set for fitting (~6.5 s of audio). */
std::string fitFeatures (const Factory& f)
{
    Json j;
    constexpr int order = 13, N = 1 << order;   // 8192
    juce::dsp::FFT fft (order);
    auto spec = [&] (const std::vector<float>& x, size_t start)
    {
        std::vector<std::complex<float>> in ((size_t) N), out ((size_t) N);
        for (int i = 0; i < N; ++i) in[(size_t) i] = x[start + (size_t) i];
        fft.perform (in.data(), out.data(), false);
        return out;
    };
    const std::vector<double> levels { -52.0, -42.0, -32.0, -22.0, -12.0 };
    for (int k0 : { 75, 19 })   // 439.5 Hz, 111.3 Hz
    {
        std::vector<float> one ((size_t) N);
        for (int i = 0; i < N; ++i) one[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * k0 * i / N);
        std::vector<double> g, h2, h3, h5;
        for (double lvl : levels)
        {
            auto x = repeat (one, 2, (float) juce::Decibels::decibelsToGain (lvl));
            auto y = run (f, x);
            auto Y = spec (y, (size_t) N);
            const double a1 = std::abs (Y[(size_t) k0]);
            g.push_back (db (a1 * 2.0 / N) - lvl);
            h2.push_back (db (std::abs (Y[(size_t) (2 * k0)]) / a1));
            h3.push_back (db (std::abs (Y[(size_t) (3 * k0)]) / a1));
            h5.push_back (db (std::abs (Y[(size_t) (5 * k0)]) / a1));
        }
        const auto tag = juce::String (k0).toStdString();
        j.arr ("g" + tag, g); j.arr ("h2_" + tag, h2); j.arr ("h3_" + tag, h3); j.arr ("h5_" + tag, h5);
    }
    {
        constexpr int mo = 14, M = 1 << mo;   // 16384: 2.93 Hz bins
        juce::dsp::FFT mfft (mo);
        std::vector<int> bins;
        int last = 0;
        for (double fr = 45.0; fr < 12000.0; fr *= std::pow (2.0, 1.0 / 3.0))
        {
            int k = (int) std::round (fr / (kRate / M)) | 1;
            if (k <= last) k = last + 2;
            bins.push_back (k);
            last = k;
        }
        std::vector<float> one ((size_t) M, 0.0f);
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> ph (0.0, juce::MathConstants<double>::twoPi);
        std::vector<double> acc ((size_t) M, 0.0);
        for (int k : bins)
        {
            const double p = ph (rng);
            for (int i = 0; i < M; ++i) acc[(size_t) i] += std::sin (juce::MathConstants<double>::twoPi * k * i / M + p);
        }
        double rms = 0.0;
        for (double v : acc) rms += v * v;
        rms = std::sqrt (rms / M);
        for (int i = 0; i < M; ++i) one[(size_t) i] = (float) (acc[(size_t) i] / rms);
        std::vector<double> fr;
        for (int k : bins) fr.push_back (k * kRate / M);
        j.arr ("mfreq", fr);
        for (double lvl : { -60.0, -40.0, -24.0 })
        {
            auto x = repeat (one, 2, (float) juce::Decibels::decibelsToGain (lvl));
            auto y = run (f, x);
            std::vector<std::complex<float>> a ((size_t) M), A ((size_t) M), b ((size_t) M), B ((size_t) M);
            for (int i = 0; i < M; ++i) { a[(size_t) i] = x[(size_t) (M + i)]; b[(size_t) i] = y[(size_t) (M + i)]; }
            mfft.perform (a.data(), A.data(), false);
            mfft.perform (b.data(), B.data(), false);
            std::vector<double> r;
            double te = 0.0, ae = 0.0;
            for (int k : bins) { r.push_back (db (std::abs (B[(size_t) k]) / std::abs (A[(size_t) k]))); te += std::norm (B[(size_t) k]); }
            for (int k = 10; k < M / 2 && k * kRate / M < 16000.0; ++k) ae += std::norm (B[(size_t) k]);
            const auto tag = juce::String ((int) -lvl).toStdString();
            j.arr ("m" + tag, r);
            j.num ("imd" + tag, 10.0 * std::log10 (std::max (1.0e-30, ae - te) / std::max (1.0e-30, te)));
        }
    }
    {
        const int n = (int) (kRate * 1.0);
        std::vector<float> x ((size_t) n, 0.0f);
        const int b0 = (int) (0.1 * kRate), b1 = (int) (0.4 * kRate);
        const float a = (float) juce::Decibels::decibelsToGain (-14.0), p = (float) juce::Decibels::decibelsToGain (-40.0);
        for (int i = b0; i < b1; ++i) x[(size_t) i] = a * (float) std::sin (juce::MathConstants<double>::twoPi * 196.0 * (i - b0) / kRate);
        for (int i = b1; i < n; ++i) x[(size_t) i] = p * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / kRate);
        auto y = run (f, x);
        std::vector<double> env;
        const int w = (int) (0.02 * kRate);
        for (int s0 = b1; s0 + w <= n; s0 += w)
        {
            double e = 0.0;
            for (int i = s0; i < s0 + w; ++i) e += (double) y[(size_t) i] * y[(size_t) i];
            env.push_back (10.0 * std::log10 (e / w + 1.0e-30));
        }
        j.arr ("recover", env);
    }
    return j.done();
}

/** --set key=value,... on the reference (channel, side): AmpDef fields, sN.field for a stage. */
void applyOverrides (int channel, int side, const juce::String& spec)
{
    auto pairs = juce::StringArray::fromTokens (spec, ",", "");
    ampsim::referenceTweak() = [channel, side, pairs] (int c, int s, ampsim::AmpDef& a)
    {
        if (c != channel || s != side) return;
        for (const auto& kv : pairs)
        {
            const auto key = kv.upToFirstOccurrenceOf ("=", false, false).trim();
            const float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            if (key.startsWith ("s") && key.contains ("."))
            {
                auto& st = a.st[(size_t) juce::jlimit (0, 3, key.substring (1, 2).getIntValue())];
                const auto f = key.fromFirstOccurrenceOf (".", false, false);
                std::map<juce::String, float*> m { { "B", &st.B }, { "Rp", &st.Rp }, { "Rk", &st.Rk }, { "fb", &st.fb }, { "Rload", &st.Rload },
                    { "ssGain", &st.ssGain }, { "ssRail", &st.ssRail }, { "Rs", &st.Rs }, { "Rg", &st.Rg }, { "C", &st.C },
                    { "cathDb", &st.cathDb }, { "cathHz", &st.cathHz }, { "lpHz", &st.lpHz }, { "div", &st.div },
                    { "divShelfDb", &st.divShelfDb }, { "divShelfHz", &st.divShelfHz } };
                if (auto it = m.find (f); it != m.end()) *it->second = v;
                else if (f == "type") st.type = (int) v;
                else { std::fprintf (stderr, "unknown key %s\n", key.toRawUTF8()); std::exit (2); }
                continue;
            }
            std::map<juce::String, float*> m { { "brightDb", &a.brightDb }, { "brightHz", &a.brightHz }, { "cf", &a.cf },
                { "R1", &a.R1 }, { "R2", &a.R2 }, { "R3", &a.R3 }, { "R4", &a.R4 }, { "C1", &a.C1 }, { "C2", &a.C2 }, { "C3", &a.C3 },
                { "voiceHz", &a.voiceHz }, { "voiceDb", &a.voiceDb }, { "voiceQ", &a.voiceQ }, { "piMax", &a.piMax }, { "bias", &a.bias },
                { "hard", &a.hard }, { "satKnee", &a.satKnee }, { "nfb", &a.nfb }, { "sag", &a.sag }, { "sagMs", &a.sagMs },
                { "presenceHz", &a.presenceHz }, { "presenceMax", &a.presenceMax }, { "depthHz", &a.depthHz }, { "depthMax", &a.depthMax },
                { "spkHz", &a.spkHz }, { "spkQ", &a.spkQ }, { "spkDb", &a.spkDb }, { "coilHz", &a.coilHz }, { "coilDb", &a.coilDb },
                { "xfHp", &a.xfHp }, { "xfLp", &a.xfLp }, { "paRef", &a.paRef }, { "outDb", &a.outDb }, { "gridKg", &a.gridKg }, { "inDb", &a.inDb } };
            if (auto it = m.find (key); it != m.end()) *it->second = v;
            else if (key == "gainPotAfter") a.gainPotAfter = (int) v;
            else { std::fprintf (stderr, "unknown key %s\n", key.toRawUTF8()); std::exit (2); }
        }
    };
    ampsim::rebuildTables();
}

std::string dumpReference (int channel, int side)
{
    const auto a = ampsim::reference (channel, side);
    Json j;
    for (int s = 0; s < 4; ++s)
    {
        const auto& st = a.st[(size_t) s];
        const auto p = "s" + std::to_string (s) + ".";
        j.num (p + "type", st.type);
        for (auto [k, v] : std::vector<std::pair<const char*, float>> { { "B", st.B }, { "Rk", st.Rk }, { "fb", st.fb }, { "Rload", st.Rload },
                 { "ssGain", st.ssGain }, { "Rs", st.Rs }, { "Rg", st.Rg }, { "C", st.C }, { "cathDb", st.cathDb }, { "cathHz", st.cathHz },
                 { "lpHz", st.lpHz }, { "div", st.div }, { "divShelfDb", st.divShelfDb }, { "divShelfHz", st.divShelfHz } })
            j.num (p + k, v);
    }
    for (auto [k, v] : std::vector<std::pair<const char*, float>> { { "brightDb", a.brightDb }, { "brightHz", a.brightHz }, { "cf", a.cf },
             { "R1", a.R1 }, { "R2", a.R2 }, { "R3", a.R3 }, { "R4", a.R4 }, { "C1", a.C1 }, { "C2", a.C2 }, { "C3", a.C3 },
             { "voiceHz", a.voiceHz }, { "voiceDb", a.voiceDb }, { "voiceQ", a.voiceQ }, { "piMax", a.piMax }, { "bias", a.bias },
             { "hard", a.hard }, { "satKnee", a.satKnee }, { "nfb", a.nfb }, { "sag", a.sag }, { "sagMs", a.sagMs },
             { "presenceHz", a.presenceHz }, { "presenceMax", a.presenceMax }, { "depthHz", a.depthHz }, { "depthMax", a.depthMax },
             { "spkHz", a.spkHz }, { "spkQ", a.spkQ }, { "spkDb", a.spkDb }, { "coilHz", a.coilHz }, { "coilDb", a.coilDb },
             { "xfHp", a.xfHp }, { "xfLp", a.xfLp }, { "paRef", a.paRef }, { "outDb", a.outDb }, { "gridKg", a.gridKg }, { "inDb", a.inDb } })
        j.num (k, v);
    return j.done();
}

bool writeWav (const juce::File& file, const std::vector<float>& data)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    if (stream == nullptr) return false;
    std::unique_ptr<juce::OutputStream> os (stream.release());
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (kRate).withNumChannels (1).withBitsPerSample (24));
    if (writer == nullptr) return false;
    const float* ch[1] { data.data() };
    return writer->writeFromFloatArrays (ch, 1, (int) data.size());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    // optional: --set <channel>:<side>:key=value,key=value (edits a reference circuit)
    std::vector<juce::String> args;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--set" && i + 1 < argc)
        {
            const juce::String spec (argv[++i]);
            auto parts = juce::StringArray::fromTokens (spec, ":", "");
            applyOverrides (parts[0].getIntValue(), parts[1].getIntValue(), spec.fromFirstOccurrenceOf (":", false, false).fromFirstOccurrenceOf (":", false, false));
            continue;
        }
        args.push_back (argv[i]);
    }
    if (args.size() >= 2 && args[0] == "features")
    {
        std::fputs (features (parseTarget (args[1])).c_str(), stdout);
        return 0;
    }
    if (args.size() >= 2 && args[0] == "fitfeat")
    {
        std::fputs (fitFeatures (parseTarget (args[1])).c_str(), stdout);
        return 0;
    }
    if (args.size() >= 5 && args[0] == "cabir")   // cabir <type> <mic 0..1> <dist 0..1> <out.wav>
    {
        auto ir = CabBlock::designIR (args[1].getIntValue(), args[2].getFloatValue(), args[3].getFloatValue(), kRate);
        CabBlock::levelIR (ir, kRate);
        std::vector<float> d (ir.getReadPointer (0), ir.getReadPointer (0) + ir.getNumSamples());
        return writeWav (juce::File (args[4]), d) ? 0 : 1;
    }
    if (args.size() >= 3 && args[0] == "dump")
    {
        std::fputs (dumpReference (args[1].getIntValue(), args[2].getIntValue()).c_str(), stdout);
        return 0;
    }
    if (argc >= 5 && juce::String (argv[1]) == "render")
    {
        auto f = parseTarget (argv[2]);
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[3])));
        if (r == nullptr) return 2;
        juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, false);
        std::vector<float> in (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
        return writeWav (juce::File (argv[4]), run (f, in)) ? 0 : 1;
    }
    if (argc >= 3 && juce::String (argv[1]) == "riff")
        return writeWav (juce::File (argv[2]), guitarRiff (8.0)) ? 0 : 1;
    std::fprintf (stderr, "usage: SwarmnessAmpLab [--set ch:side:k=v,...] features|fitfeat <target> | dump <ch> <side> | render <target> <in.wav> <out.wav> | riff <out.wav>\n");
    return 2;
}
