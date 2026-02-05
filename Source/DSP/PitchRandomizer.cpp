#include "PitchRandomizer.h"

void PitchRandomizer::prepare(double sampleRate) {
    mSampleRate = sampleRate;
    mSmoothedOutput.reset(sampleRate, 0.05);
    reset();
}

void PitchRandomizer::reset() {
    mPhase = 0.0f;
    mCurrentTarget = 0.0f;
    mPreviousTarget = 0.0f;
    mCurrentValue = 0.0f;
    mSmoothedOutput.setCurrentAndTargetValue(0.0f);
}

void PitchRandomizer::setRandomRange(float semitones) {
    mRandomRange = juce::jlimit(0.0f, 24.0f, semitones);
}

void PitchRandomizer::setRandomRate(float hz) {
    mRandomRate = juce::jlimit(0.1f, 10.0f, hz);
}

void PitchRandomizer::setSmooth(float amount) {
    mSmooth = juce::jlimit(0.0f, 1.0f, amount);
    // Adjust smoothing time based on amount
    mSmoothedOutput.reset(mSampleRate, 0.01 + mSmooth * 0.2);
}

void PitchRandomizer::setMode(Mode mode) {
    mMode = mode;
}

void PitchRandomizer::setSeed(uint32_t seed) {
    mRandom.setSeed(static_cast<juce::int64>(seed));
}

float PitchRandomizer::process() {
    if (mRandomRange <= 0.0f) {
        return 0.0f;
    }

    float phaseIncrement = mRandomRate / static_cast<float>(mSampleRate);
    mPhase += phaseIncrement;

    // Generate new random target at each cycle
    if (mPhase >= 1.0f) {
        mPhase -= 1.0f;
        mPreviousTarget = mCurrentTarget;
        // Random value between -range and +range
        mCurrentTarget = (mRandom.nextFloat() * 2.0f - 1.0f) * mRandomRange;
    }

    float output;
    if (mMode == Jump) {
        // Jump mode: instant change with optional smoothing
        output = mCurrentTarget;
    } else {
        // Glide mode: interpolate between targets using S-curve
        float t = mPhase;
        // Smoothstep for natural glide
        t = t * t * (3.0f - 2.0f * t);
        output = mPreviousTarget + (mCurrentTarget - mPreviousTarget) * t;
    }

    mSmoothedOutput.setTargetValue(output);
    return mSmoothedOutput.getNextValue();
}
