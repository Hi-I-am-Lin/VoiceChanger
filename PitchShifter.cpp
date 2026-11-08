#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float PI = 3.14159265358979f;

// Keeps linear interpolation from reading the sample that is being overwritten.
constexpr float MIN_DELAY_SAMPLES = 2.0f;
}

void PitchShifter::Prepare(double sampleRate, double windowMs)
{
    windowSamples_ = static_cast<float>(sampleRate * windowMs / 1000.0);

    // A power-of-two size lets the circular index wrap with a bit mask.
    size_t size = 1;
    while (size < static_cast<size_t>(windowSamples_ + MIN_DELAY_SAMPLES) + 4)
    {
        size <<= 1;
    }
    buffer_.assign(size, 0.0f);
    mask_ = size - 1;
    writeIndex_ = 0;
    phase_ = 0.0f;
}

void PitchShifter::SetSemitones(float semitones)
{
    ratio_ = std::pow(2.0f, semitones / 12.0f);
}

float PitchShifter::ReadDelayed(float delaySamples) const
{
    float readPos = static_cast<float>(writeIndex_) - delaySamples;
    if (readPos < 0.0f)
    {
        readPos += static_cast<float>(buffer_.size());
    }

    const size_t i0 = static_cast<size_t>(readPos);
    const float frac = readPos - static_cast<float>(i0);
    const float s0 = buffer_[i0 & mask_];
    const float s1 = buffer_[(i0 + 1) & mask_];
    return s0 + frac * (s1 - s0);
}

float PitchShifter::Process(float input)
{
    buffer_[writeIndex_] = input;

    float output = 0.0f;
    if (ratio_ == 1.0f)
    {
        // Same delay as tap 2 at phase 0, so enabling a shift later continues seamlessly.
        phase_ = 0.0f;
        output = ReadDelayed(windowSamples_ * 0.5f + MIN_DELAY_SAMPLES);
    }
    else
    {
        // The delay changes by (1 - ratio) samples per output sample,
        // so the read pointer moves at `ratio` times the write speed.
        phase_ += (1.0f - ratio_) / windowSamples_;
        phase_ -= std::floor(phase_);

        float phase2 = phase_ + 0.5f;
        if (phase2 >= 1.0f)
        {
            phase2 -= 1.0f;
        }

        // sin^2 windows offset by half a period always sum to 1.
        // A tap is silent exactly when its delay jumps across the window edge.
        const float s1 = std::sin(PI * phase_);
        const float s2 = std::sin(PI * phase2);
        output = s1 * s1 * ReadDelayed(phase_ * windowSamples_ + MIN_DELAY_SAMPLES)
            + s2 * s2 * ReadDelayed(phase2 * windowSamples_ + MIN_DELAY_SAMPLES);
    }

    writeIndex_ = (writeIndex_ + 1) & mask_;
    return output;
}
