#pragma once
#include <JuceHeader.h>

class PitchRandomizer {
public:
    enum Mode { Jump, Glide };

    PitchRandomizer() = default;
    ~PitchRandomizer() = default;

    void prepare(double sampleRate);
    void reset();
    void setRandomRange(float semitones);  // 0-24
    void setRandomRate(float hz);          // 0.1-10
    void setSmooth(float amount);          // 0-1
    void setMode(Mode mode);
    void setSeed(uint32_t seed);
    float process();  // Returns pitch offset in semitones

private:
    double mSampleRate = 44100.0;
    float mRandomRange = 0.0f;
    float mRandomRate = 1.0f;
    float mSmooth = 0.5f;
    Mode mMode = Jump;

    juce::Random mRandom;
    float mPhase = 0.0f;
    float mCurrentTarget = 0.0f;
    float mPreviousTarget = 0.0f;
    float mCurrentValue = 0.0f;
    
    juce::SmoothedValue<float> mSmoothedOutput{0.0f};
};
