#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <span>
#include <vector>

namespace sv::synthesis
{
struct PeriodicExcitationFrame
{
    float fundamentalFrequencyHz = 0.0f;
    float shape = 1.0f;
    bool voiced = false;
};

// Offline LF excitation, before the learned spectral filters. Each frame contributes
// a two-hop Hann window. The first half-window is discarded and the last is retained,
// giving frames.size() * hopSamples samples. phaseCycles contains the cumulative,
// unwrapped phase at the end of each frame, including unvoiced frames.
[[nodiscard]] juce::Result renderPeriodicExcitation(std::span<const PeriodicExcitationFrame> frames,
                                                    std::size_t hopSamples,
                                                    double framePeriodSeconds,
                                                    float gain,
                                                    std::vector<float>& samples,
                                                    std::vector<double>& phaseCycles);

// Parameters contain one frame-major row per frame: log amplitudes for all ratios,
// followed by their phase offsets in radians. This is the additional periodic
// modulation signal; multiplication by the excitation and the model's DC offset
// belongs to the caller. Both render functions leave outputs unchanged on failure.
[[nodiscard]] juce::Result renderPeriodicModulation(std::span<const PeriodicExcitationFrame> frames,
                                                    std::span<const double> phaseCycles,
                                                    std::span<const float> frequencyRatios,
                                                    std::span<const float> parameters,
                                                    std::size_t hopSamples,
                                                    double framePeriodSeconds,
                                                    std::vector<float>& samples);
} // namespace sv::synthesis
