#pragma once

/**
 * Portability layer for the Swarmness DSP engine.
 *
 * The DSP code uses the small set of utilities below through the namespace sw::.
 *  - In the plug-in they are JUCE's own classes (sw:: just re-exports them).
 *  - With SWARM_NO_JUCE defined (pedal firmware, bare-metal / embedded Linux builds) they are
 *    the self-contained implementations in this file, written to behave exactly like JUCE's,
 *    so the engine sounds identical on both.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#if ! defined (SWARM_NO_JUCE)

#include <JuceHeader.h>

namespace sw
{
    using juce::jmin;
    using juce::jmax;
    using juce::jlimit;
    using juce::nextPowerOfTwo;
    using juce::exactlyEqual;
    using juce::approximatelyEqual;
    using juce::MathConstants;
    using juce::SmoothedValue;
    using juce::AudioBuffer;
    using juce::FloatVectorOperations;
    using juce::uint32;
    namespace dsp = juce::dsp;
}

#else

namespace sw
{
    using uint32 = std::uint32_t;

    template <typename T> constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }
    template <typename T> constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }
    template <typename T> constexpr T jmin (T a, T b, T c) noexcept { return jmin (jmin (a, b), c); }
    template <typename T> constexpr T jmax (T a, T b, T c) noexcept { return jmax (jmax (a, b), c); }
    template <typename T> constexpr T jlimit (T lower, T upper, T v) noexcept { return v < lower ? lower : (upper < v ? upper : v); }

    inline int nextPowerOfTwo (int n) noexcept
    {
        --n;
        n |= (n >> 1); n |= (n >> 2); n |= (n >> 4); n |= (n >> 8); n |= (n >> 16);
        return n + 1;
    }

    template <typename T> constexpr bool exactlyEqual (T a, T b) noexcept { return ! (a < b) && ! (b < a); }

    template <typename T>
    inline bool approximatelyEqual (T a, T b) noexcept
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            if (! (std::isfinite (a) && std::isfinite (b)))
                return exactlyEqual (a, b);
            const auto diff = std::abs (a - b);
            return diff <= std::numeric_limits<T>::min()
                || diff <= std::numeric_limits<T>::epsilon() * std::max (std::abs (a), std::abs (b));
        }
        else
        {
            return a == b;
        }
    }

    template <typename T>
    struct MathConstants
    {
        static constexpr T pi = static_cast<T> (3.141592653589793238L);
        static constexpr T twoPi = static_cast<T> (2 * 3.141592653589793238L);
        static constexpr T halfPi = static_cast<T> (3.141592653589793238L / 2);
        static constexpr T sqrt2 = static_cast<T> (1.4142135623730950488L);
    };

    /** Linear ramp, same behaviour as juce::SmoothedValue<float, Linear>. */
    template <typename T>
    class SmoothedValue
    {
    public:
        SmoothedValue() noexcept = default;
        explicit SmoothedValue (T initial) noexcept : current (initial), target (initial) {}

        void reset (double sampleRate, double rampSeconds) noexcept { reset ((int) std::floor (rampSeconds * sampleRate)); }
        void reset (int numSteps) noexcept { stepsToTarget = numSteps; setCurrentAndTargetValue (target); }

        void setCurrentAndTargetValue (T v) noexcept { current = target = v; countdown = 0; }

        void setTargetValue (T v) noexcept
        {
            if (approximatelyEqual (v, target))
                return;
            if (stepsToTarget <= 0)
            {
                setCurrentAndTargetValue (v);
                return;
            }
            target = v;
            countdown = stepsToTarget;
            step = (target - current) / (T) countdown;
        }

        T getNextValue() noexcept
        {
            if (! isSmoothing())
                return target;
            --countdown;
            if (isSmoothing()) current += step;
            else               current = target;
            return current;
        }

        T skip (int numSamples) noexcept
        {
            if (numSamples >= countdown)
            {
                setCurrentAndTargetValue (target);
                return target;
            }
            current += step * (T) numSamples;
            countdown -= numSamples;
            return current;
        }

        bool isSmoothing() const noexcept { return countdown > 0; }
        T getCurrentValue() const noexcept { return current; }
        T getTargetValue() const noexcept  { return target; }

    private:
        T current = 0, target = 0, step = 0;
        int countdown = 0, stepsToTarget = 0;
    };

    /** Minimal multichannel sample buffer (the subset of juce::AudioBuffer the engine uses). */
    template <typename T>
    class AudioBuffer
    {
    public:
        AudioBuffer() = default;
        AudioBuffer (int channels, int samples) { setSize (channels, samples); }

        void setSize (int channels, int samples, bool = false, bool = false, bool = false)
        {
            numChannels = channels;
            numSamples = samples;
            data.assign ((size_t) (channels * samples), T());
        }

        int getNumChannels() const noexcept { return numChannels; }
        int getNumSamples() const noexcept  { return numSamples; }
        T* getWritePointer (int ch) noexcept             { return data.data() + (size_t) (ch * numSamples); }
        T* getWritePointer (int ch, int start) noexcept  { return getWritePointer (ch) + start; }
        const T* getReadPointer (int ch) const noexcept  { return data.data() + (size_t) (ch * numSamples); }
        const T* getReadPointer (int ch, int start) const noexcept { return getReadPointer (ch) + start; }
        T getSample (int ch, int i) const noexcept       { return getReadPointer (ch)[i]; }
        void setSample (int ch, int i, T v) noexcept     { getWritePointer (ch)[i] = v; }
        void clear() noexcept                            { std::fill (data.begin(), data.end(), T()); }
        void clear (int ch, int start, int n) noexcept   { std::fill_n (getWritePointer (ch) + start, n, T()); }
        void copyFrom (int ch, int start, const T* src, int n) noexcept { std::copy_n (src, n, getWritePointer (ch) + start); }

    private:
        std::vector<T> data;
        int numChannels = 0, numSamples = 0;
    };

    struct FloatVectorOperations
    {
        static void clear (float* d, int n) noexcept                        { std::fill_n (d, n, 0.0f); }
        static void copy (float* d, const float* s, int n) noexcept         { std::copy_n (s, n, d); }
        static void multiply (float* d, float g, int n) noexcept            { for (int i = 0; i < n; ++i) d[i] *= g; }
    };

    namespace dsp
    {
        struct ProcessSpec
        {
            double sampleRate;
            uint32 maximumBlockSize;
            uint32 numChannels;
        };

        namespace DelayLineInterpolationTypes
        {
            struct None {};
            struct Linear {};
        }

        /** Same indexing and interpolation as juce::dsp::DelayLine (None / Linear). */
        template <typename T, typename Interpolation>
        class DelayLine
        {
        public:
            explicit DelayLine (int maxDelay = 0) { setMaximumDelayInSamples (maxDelay); }

            void setMaximumDelayInSamples (int maxDelay)
            {
                totalSize = jmax (4, maxDelay + 2);
                buffer.setSize (buffer.getNumChannels(), totalSize);
                reset();
            }
            int getMaximumDelayInSamples() const noexcept { return totalSize - 2; }

            void prepare (const ProcessSpec& spec)
            {
                buffer.setSize ((int) spec.numChannels, totalSize);
                writePos.assign (spec.numChannels, 0);
                readPos.assign (spec.numChannels, 0);
                reset();
            }

            void reset()
            {
                std::fill (writePos.begin(), writePos.end(), 0);
                std::fill (readPos.begin(), readPos.end(), 0);
                buffer.clear();
            }

            void setDelay (T d) noexcept
            {
                delay = jlimit ((T) 0, (T) getMaximumDelayInSamples(), d);
                delayInt = (int) std::floor (delay);
                delayFrac = delay - (T) delayInt;
            }

            void pushSample (int ch, T x) noexcept
            {
                buffer.setSample (ch, writePos[(size_t) ch], x);
                writePos[(size_t) ch] = (writePos[(size_t) ch] + totalSize - 1) % totalSize;
            }

            T popSample (int ch, T newDelay = -1, bool updateReadPointer = true) noexcept
            {
                if (newDelay >= 0)
                    setDelay (newDelay);

                T result;
                if constexpr (std::is_same_v<Interpolation, DelayLineInterpolationTypes::None>)
                {
                    result = buffer.getSample (ch, (readPos[(size_t) ch] + delayInt) % totalSize);
                }
                else
                {
                    int i1 = readPos[(size_t) ch] + delayInt, i2 = i1 + 1;
                    if (i2 >= totalSize) { i1 %= totalSize; i2 %= totalSize; }
                    const T v1 = buffer.getSample (ch, i1), v2 = buffer.getSample (ch, i2);
                    result = v1 + delayFrac * (v2 - v1);
                }

                if (updateReadPointer)
                    readPos[(size_t) ch] = (readPos[(size_t) ch] + totalSize - 1) % totalSize;
                return result;
            }

        private:
            AudioBuffer<T> buffer;
            std::vector<int> writePos, readPos;
            T delay = 0, delayFrac = 0;
            int delayInt = 0, totalSize = 4;
        };
    }
}

#endif
