#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

/**
 * ChorusEngine - Stereo chorus with Classic and Deep modes
 * v1.2.8: Optimized with sin LUT and linear interpolation option
 */
class ChorusEngine {
public:
    enum Mode {
        Classic = 0,    // Standard chorus
        Deep           // Wider, more lush chorus with modulated stereo
    };

    static constexpr int kLUTSize = 2048;
    static constexpr float kLUTMask = static_cast<float>(kLUTSize - 1);

    ChorusEngine() {
        // v1.2.8: Initialize sin LUT
        for (int i = 0; i < kLUTSize; ++i) {
            mSinLUT[i] = std::sin(juce::MathConstants<float>::twoPi * i / kLUTSize);
        }
    }
    ~ChorusEngine() = default;

    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();
    void setMode(Mode mode);     // Classic or Deep
    void setRate(float hz);      // 0.1-5 Hz
    void setDepth(float depth);  // 0-1
    void setMix(float mix);      // 0-1
    void setFeedback(float fb);  // 0-1
    void process(juce::AudioBuffer<float>& buffer);

private:
    static constexpr int kNumVoices = 3;
    static constexpr int kMaxDelayLength = 4410; // 100ms @ 44.1k

    // v1.2.8: Linear interpolation (faster than hermite)
    float linearInterpolate(const std::vector<float>& buffer, float pos);
    // Hermite kept for Deep mode (better quality)
    float hermiteInterpolate(const std::vector<float>& buffer, float pos);
    
    // v1.2.8: Fast sin using LUT
    inline float fastSin(float phase) const {
        float indexF = phase * kLUTMask;
        int idx0 = static_cast<int>(indexF) & (kLUTSize - 1);
        int idx1 = (idx0 + 1) & (kLUTSize - 1);
        float frac = indexF - static_cast<float>(static_cast<int>(indexF));
        return mSinLUT[idx0] + frac * (mSinLUT[idx1] - mSinLUT[idx0]);
    }

    std::array<float, kLUTSize> mSinLUT;
    
    double mSampleRate = 44100.0;
    Mode mMode = Classic;
    float mRate = 1.0f;
    float mDepth = 0.5f;
    float mMix = 0.0f;
    float mFeedback = 0.0f;

    std::array<std::vector<float>, 2> mDelayBuffer;
    int mWritePos = 0;

    std::array<float, kNumVoices> mLFOPhases = {0.0f, 0.33f, 0.66f};
    
    juce::SmoothedValue<float> mSmoothMix{0.0f};
};
