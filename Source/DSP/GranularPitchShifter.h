#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

class GranularPitchShifter {
public:
    GranularPitchShifter() = default;
    ~GranularPitchShifter() = default;

    void prepare(double sampleRate, int blockSize);
    void reset();
    void setOctaveMode(int mode); // 0: -2 oct, 1: -1 oct, 2: +1 oct, 3: +2 oct
    void setEngage(bool engaged);
    void setRiseTime(float ms);
    void setPanic(float amount);     // 0-1
    void setChaos(float amount);     // 0-1
    void setDynamicPitchOffset(float semitones);
    void process(juce::AudioBuffer<float>& buffer);

private:
    static constexpr int kNumGrains = 4;
    static constexpr int kMaxDelayLength = 88200; // 2 seconds at 44.1k
    static constexpr int kDefaultGrainSize = 2048;

    struct Grain {
        float phase = 0.0f;
        float readPos = 0.0f;
        int grainSize = kDefaultGrainSize;
        bool active = false;
    };

    float hermiteInterpolate(float* buffer, int bufferSize, float pos);
    float hannWindow(float phase);

    double mSampleRate = 44100.0;
    int mBlockSize = 512;

    std::array<std::vector<float>, 2> mDelayBuffer;
    int mWritePos = 0;

    std::array<Grain, kNumGrains> mGrains;
    int mGrainSize = kDefaultGrainSize;

    int mOctaveMode = 0;       // 0: +1 oct, 1: +2 oct, 2: -1 oct
    bool mEngaged = true;
    float mRiseTime = 100.0f;  // ms
    float mPanic = 0.0f;
    float mChaos = 0.0f;
    float mDynamicOffset = 0.0f;

    juce::SmoothedValue<float> mPitchRatio{1.0f};
    juce::SmoothedValue<float> mWetGain{1.0f};
    
    juce::Random mRandom;
    float mChaosLFO = 0.0f;
    float mChaosPhase = 0.0f;
};
