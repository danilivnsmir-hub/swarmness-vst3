#pragma once
#include <JuceHeader.h>

class PitchSlideEngine {
public:
    enum Direction { Up, Down, Both };

    PitchSlideEngine() = default;
    ~PitchSlideEngine() = default;

    void prepare(double sampleRate);
    void reset();
    void setSlideRange(float semitones);   // -24 to +24
    void setSlideTime(float ms);           // 50-5000
    void setDirection(Direction dir);
    void setAutoSlide(bool enabled);
    void setPosition(float pos);           // 0-1 for manual control
    void setReturn(bool enabled);
    void trigger();
    void release();
    float process();  // Returns current pitch offset in semitones

private:
    float sCurve(float x);  // S-curve for natural glides

    double mSampleRate = 44100.0;
    float mSlideRange = 12.0f;
    float mSlideTime = 500.0f;
    Direction mDirection = Up;
    bool mAutoSlide = false;
    bool mReturn = true;
    float mManualPosition = 0.0f;

    juce::SmoothedValue<float> mCurrentOffset{0.0f};
    float mEnvelopePhase = 0.0f;
    bool mTriggered = false;
    bool mReleasing = false;
    float mAutoPhase = 0.0f;
};
