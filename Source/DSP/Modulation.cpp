#include "Modulation.h"
#include <cmath>

void Modulation::prepare(double sampleRate) {
    mSampleRate = sampleRate;
    reset();
}

void Modulation::reset() {
    mLFOPhase = 0.0f;
    mSampleHoldValue = 0.0f;
    mSampleHoldCounter = 0.0f;
}

void Modulation::setLFORate(float normalizedSpeed) {
    // Exponential mapping: 0->0.5Hz, 0.5->5Hz, 1->50Hz
    normalizedSpeed = juce::jlimit(0.0f, 1.0f, normalizedSpeed);
    mLFORate = 0.5f * std::pow(100.0f, normalizedSpeed);
}

void Modulation::setLFODepth(float depth) {
    mLFODepth = juce::jlimit(0.0f, 1.0f, depth);
}

void Modulation::setRandomAmount(float amount) {
    mRandomAmount = juce::jlimit(0.0f, 1.0f, amount);
}

float Modulation::getNextModulationValue() {
    // LFO component (sine wave)
    float lfoValue = std::sin(mLFOPhase * juce::MathConstants<float>::twoPi);
    mLFOPhase += mLFORate / static_cast<float>(mSampleRate);
    if (mLFOPhase >= 1.0f) mLFOPhase -= 1.0f;

    // Sample-and-hold random component
    mSampleHoldCounter += mLFORate * 2.0f / static_cast<float>(mSampleRate);
    if (mSampleHoldCounter >= 1.0f) {
        mSampleHoldCounter -= 1.0f;
        mSampleHoldValue = mRandom.nextFloat() * 2.0f - 1.0f;
    }

    // Combine LFO and random
    float output = lfoValue * mLFODepth;
    output += mSampleHoldValue * mRandomAmount;

    return juce::jlimit(-1.0f, 1.0f, output);
}
