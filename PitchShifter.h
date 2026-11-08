#pragma once

#include <cstddef>
#include <vector>

// Real-time pitch shifter built from two crossfaded taps on a delay line.
// Each tap reads the input at a sweeping delay, which resamples it by `ratio`.
// When one tap reaches the end of the window, the other tap takes over.
//
// Latency is about half the window length (20 ms with the default window).
// Formants move with the pitch, so large shifts sound like a chipmunk or a giant.
class PitchShifter
{
public:
    void Prepare(double sampleRate, double windowMs = 40.0);
    void SetSemitones(float semitones);
    float Process(float input);

private:
    float ReadDelayed(float delaySamples) const;

    std::vector<float> buffer_;
    size_t mask_ = 0;
    size_t writeIndex_ = 0;
    float windowSamples_ = 0.0f;
    float phase_ = 0.0f;
    float ratio_ = 1.0f;
};
