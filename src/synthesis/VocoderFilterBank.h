#pragma once

#include "DnniInference.h"
#include "DnniReader.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <span>
#include <vector>

namespace sv::synthesis
{
class VocoderFilterBank
{
public:
    [[nodiscard]] juce::Result load(const DnniReader& reader, std::size_t filterNode, std::size_t hopSamples);
    // Whole-sequence filtering outside the audio callback. Input and output have frames * hopSamples samples.
    // The spectral features are the network's weighted cepstral coefficients, not magnitude bins.
    [[nodiscard]] juce::Result render(const DnniTensor& spectralFeatures, std::span<const float> excitation, std::vector<float>& output) const;

private:
    std::size_t hopSamples = 0;
    std::size_t fftSize = 0;
    int fftOrder = 0;
    std::vector<float> analysisWindow;
};
} // namespace sv::synthesis
