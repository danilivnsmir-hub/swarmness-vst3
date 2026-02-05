#include "PitchSlideEngine.h"

void PitchSlideEngine::prepare(double sampleRate) {
    mSampleRate = sampleRate;
    mCurrentOffset.reset(sampleRate, 0.01);
    reset();
}

void PitchSlideEngine::reset() {
    mEnvelopePhase = 0.0f;
    mTriggered = false;
    mReleasing = false;
    mAutoPhase = 0.0f;
    mCurrentOffset.setCurrentAndTargetValue(0.0f);
}

void PitchSlideEngine::setSlideRange(float semitones) {
    mSlideRange = juce::jlimit(-24.0f, 24.0f, semitones);
}

void PitchSlideEngine::setSlideTime(float ms) {
    mSlideTime = juce::jlimit(50.0f, 5000.0f, ms);
}

void PitchSlideEngine::setDirection(Direction dir) {
    mDirection = dir;
}

void PitchSlideEngine::setAutoSlide(bool enabled) {
    mAutoSlide = enabled;
}

void PitchSlideEngine::setPosition(float pos) {
    mManualPosition = juce::jlimit(0.0f, 1.0f, pos);
}

void PitchSlideEngine::setReturn(bool enabled) {
    mReturn = enabled;
}

void PitchSlideEngine::trigger() {
    mTriggered = true;
    mReleasing = false;
    mEnvelopePhase = 0.0f;
}

void PitchSlideEngine::release() {
    if (mReturn) {
        mReleasing = true;
    }
}

float PitchSlideEngine::sCurve(float x) {
    // Smooth S-curve using smoothstep
    x = juce::jlimit(0.0f, 1.0f, x);
    return x * x * (3.0f - 2.0f * x);
}

float PitchSlideEngine::process() {
    float targetOffset = 0.0f;

    if (mAutoSlide) {
        // Auto slide mode - continuous oscillation
        float phaseIncrement = 1.0f / (mSlideTime * 0.001f * static_cast<float>(mSampleRate));
        mAutoPhase += phaseIncrement;
        if (mAutoPhase >= 1.0f) mAutoPhase -= 1.0f;

        float curveValue;
        switch (mDirection) {
            case Up:
                curveValue = sCurve(mAutoPhase);
                targetOffset = curveValue * mSlideRange;
                break;
            case Down:
                curveValue = sCurve(mAutoPhase);
                targetOffset = -curveValue * mSlideRange;
                break;
            case Both:
                // Triangle wave for up/down
                curveValue = mAutoPhase < 0.5f ? mAutoPhase * 2.0f : (1.0f - mAutoPhase) * 2.0f;
                curveValue = sCurve(curveValue);
                targetOffset = (curveValue * 2.0f - 1.0f) * mSlideRange;
                break;
        }
    } else if (mTriggered) {
        // Manual trigger mode
        float phaseIncrement = 1.0f / (mSlideTime * 0.001f * static_cast<float>(mSampleRate));
        
        if (!mReleasing) {
            mEnvelopePhase += phaseIncrement;
            if (mEnvelopePhase >= 1.0f) {
                mEnvelopePhase = 1.0f;
            }
        } else {
            mEnvelopePhase -= phaseIncrement;
            if (mEnvelopePhase <= 0.0f) {
                mEnvelopePhase = 0.0f;
                mTriggered = false;
                mReleasing = false;
            }
        }

        float curveValue = sCurve(mEnvelopePhase);
        switch (mDirection) {
            case Up:
                targetOffset = curveValue * mSlideRange;
                break;
            case Down:
                targetOffset = -curveValue * mSlideRange;
                break;
            case Both:
                targetOffset = curveValue * mSlideRange;
                break;
        }
    } else {
        // Manual position control
        float curveValue = sCurve(mManualPosition);
        switch (mDirection) {
            case Up:
                targetOffset = curveValue * mSlideRange;
                break;
            case Down:
                targetOffset = -curveValue * mSlideRange;
                break;
            case Both:
                targetOffset = (curveValue * 2.0f - 1.0f) * mSlideRange;
                break;
        }
    }

    mCurrentOffset.setTargetValue(targetOffset);
    return mCurrentOffset.getNextValue();
}
