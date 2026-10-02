#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <span>

namespace sv::synthesis
{
struct LipRadiationResponse
{
    double amplitudeGain = 0.0;
    double phaseShiftRadians = 0.0;
};

// Frequency is in Hz and lip-opening radius is in cm. Both must be finite and
// positive. DC has no invertible response and is outside this interface.
[[nodiscard]] std::optional<LipRadiationResponse> getLipRadiationResponse(double frequencyHz, double radiusCm) noexcept;

// Harmonics are ordered from the fundamental upwards. Amplitudes are linear,
// phases are radians, and the spans must be disjoint and have equal lengths. Validation
// finishes before either array is changed. This is a spectral DSP operation;
// it does not load a voice database or generate a singing voice.
[[nodiscard]] juce::Result applyLipRadiationToHarmonics(std::span<float> amplitudes,
                                                        std::span<float> phasesRadians,
                                                        double fundamentalFrequencyHz,
                                                        double radiusCm,
                                                        bool inverse = false);
} // namespace sv::synthesis
