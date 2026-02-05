#pragma once
#include <JuceHeader.h>

class DCBlocker {
public:
    DCBlocker() = default;
    ~DCBlocker() = default;

    void prepare(double sampleRate);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);

private:
    double mSampleRate = 44100.0;
    float mCoeff = 0.0f;
    std::array<float, 2> mX1 = {0.0f, 0.0f};
    std::array<float, 2> mY1 = {0.0f, 0.0f};
};
