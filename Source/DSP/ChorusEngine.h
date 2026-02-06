#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

class ChorusEngine {
public:
    enum Mode {
        Classic = 0,    // Standard chorus
        Deep           // Wider, more lush chorus with modulated stereo
    };

    ChorusEngine() = default;
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

    float hermiteInterpolate(const std::vector<float>& buffer, float pos);

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
