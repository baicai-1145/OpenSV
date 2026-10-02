#include "VocoderFilterBank.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace sv::synthesis
{
namespace
{
constexpr std::size_t maximumFftSize = 1024 * 1024;
constexpr std::size_t maximumTensorElements = 128 * 1024 * 1024;

bool isFilterType(std::uint64_t typeId)
{
    // The registry contains twelve seeded identifiers for the same filter implementation.
    constexpr std::array<std::uint64_t, 12> identifiers{
        0x71063daca00b5f25ULL, 0x452e15e3aced1393ULL, 0x35bd434052b63ee4ULL, 0xcb4113ab3ac4b8e7ULL, 0x7380a4859db53c43ULL, 0x587c9e373dc5e0d4ULL, 0xfa929fbbaf89ff69ULL, 0x0892faf722f35001ULL, 0x33e2c6b7de8dd6a3ULL, 0x2dcfb47bd6bb27fcULL, 0x86627a39c3a17f9eULL, 0xa1ce8f87c4fa90caULL};
    return std::find(identifiers.begin(), identifiers.end(), typeId) != identifiers.end();
}

float getBoundaryWindow(std::size_t sample, std::size_t fftSize)
{
    if (sample < 16)
    {
        return static_cast<float>(sample + 1) / 16.0f;
    }
    if (sample >= fftSize - 16)
    {
        return static_cast<float>(fftSize - 1 - sample) / 16.0f;
    }
    return 1.0f;
}
} // namespace

juce::Result VocoderFilterBank::load(const DnniReader& reader, std::size_t filterNode, std::size_t requestedHopSamples)
{
    const auto& nodes = reader.getNodes();
    if (filterNode >= nodes.size() || !isFilterType(nodes[filterNode].typeId) || nodes[filterNode].payloadSize != 13 || !nodes[filterNode].children.empty())
    {
        return juce::Result::fail("Vocoder filtering requires a supported 13-byte leaf filter node.");
    }
    const auto payload = reader.getPayload(filterNode);
    const std::uint32_t windowFrames = static_cast<std::uint32_t>(payload[0]) | (static_cast<std::uint32_t>(payload[1]) << 8) | (static_cast<std::uint32_t>(payload[2]) << 16) | (static_cast<std::uint32_t>(payload[3]) << 24);
    // The reference factory consumes only this first word; the remaining nine bytes do not configure the runtime filter.
    if (windowFrames != 2 || requestedHopSamples < 8 || requestedHopSamples > maximumFftSize / 4)
    {
        return juce::Result::fail("Unsupported vocoder filter window or sample hop; the verified filter uses two frames per window.");
    }

    VocoderFilterBank candidate;
    candidate.hopSamples = requestedHopSamples;
    const std::size_t windowSamples = requestedHopSamples * windowFrames;
    candidate.fftSize = 2 * std::bit_ceil(windowSamples - 1);
    candidate.fftOrder = static_cast<int>(std::countr_zero(candidate.fftSize));
    candidate.analysisWindow.resize(windowSamples);
    float windowSum = 0.0f;
    for (std::size_t sample = 0; sample < windowSamples; sample += requestedHopSamples)
    {
        const float phase = static_cast<float>(sample) * (2.0f * std::numbers::pi_v<float>) / static_cast<float>(windowSamples);
        windowSum += 0.5f - 0.5f * std::cos(phase);
    }
    for (std::size_t sample = 0; sample < windowSamples; ++sample)
    {
        const float phase = static_cast<float>(sample) * (2.0f * std::numbers::pi_v<float>) / static_cast<float>(windowSamples);
        candidate.analysisWindow[sample] = (0.5f - 0.5f * std::cos(phase)) / windowSum;
    }
    *this = std::move(candidate);
    return juce::Result::ok();
}

juce::Result VocoderFilterBank::render(const DnniTensor& spectralFeatures, std::span<const float> excitation, std::vector<float>& output) const
{
    if (hopSamples == 0)
    {
        return juce::Result::fail("Vocoder filtering has not been loaded.");
    }
    if (spectralFeatures.channels < 4 || spectralFeatures.channels > fftSize || spectralFeatures.channels % 2 != 0 || spectralFeatures.frames > maximumTensorElements / spectralFeatures.channels || spectralFeatures.frames * spectralFeatures.channels != spectralFeatures.values.size() || spectralFeatures.frames > maximumTensorElements / hopSamples || spectralFeatures.frames * hopSamples != excitation.size())
    {
        return juce::Result::fail("Vocoder filter features and excitation have incompatible shapes.");
    }
    if (!std::all_of(spectralFeatures.values.begin(), spectralFeatures.values.end(), [](float value)
                     { return std::isfinite(value); }) ||
        !std::all_of(excitation.begin(), excitation.end(), [](float value)
                     { return std::isfinite(value); }))
    {
        return juce::Result::fail("Vocoder filter inputs contain a non-finite value.");
    }

    std::vector<float> rendered(excitation.size(), 0.0f);
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> spectrum(fftSize * 2);
    std::vector<float> signal(fftSize * 2);
    const std::size_t halfChannels = spectralFeatures.channels / 2;
    const std::size_t paddingSamples = (fftSize - analysisWindow.size()) / 2;

    for (std::size_t frame = 0; frame < spectralFeatures.frames; ++frame)
    {
        std::fill(spectrum.begin(), spectrum.end(), 0.0f);
        const auto* features = spectralFeatures.values.data() + frame * spectralFeatures.channels;
        for (std::size_t coefficient = 0; coefficient <= halfChannels; ++coefficient)
        {
            spectrum[coefficient] = (10.0f * features[coefficient]) / static_cast<float>(coefficient + 1);
        }
        for (std::size_t coefficient = 1; coefficient < halfChannels; ++coefficient)
        {
            spectrum[fftSize - halfChannels + coefficient] = (10.0f * features[halfChannels + coefficient]) / static_cast<float>(halfChannels + 1 - coefficient);
        }
        // Using JUCE's negative-exponent convention for both transforms preserves the reference's complex product.
        fft.performRealOnlyForwardTransform(spectrum.data(), true);

        std::fill(signal.begin(), signal.end(), 0.0f);
        const auto frameSample = static_cast<std::int64_t>(frame * hopSamples);
        const auto sourceStart = frameSample - static_cast<std::int64_t>(hopSamples);
        for (std::size_t sample = 0; sample < analysisWindow.size(); ++sample)
        {
            const auto sourceSample = sourceStart + static_cast<std::int64_t>(sample);
            if (sourceSample >= 0 && sourceSample < static_cast<std::int64_t>(excitation.size()))
            {
                signal[paddingSamples + sample] = excitation[static_cast<std::size_t>(sourceSample)] * analysisWindow[sample];
            }
        }
        fft.performRealOnlyForwardTransform(signal.data(), true);
        for (std::size_t bin = 0; bin <= fftSize / 2; ++bin)
        {
            const float amplitude = std::exp(spectrum[bin * 2]);
            const float phase = spectrum[bin * 2 + 1];
            if (!std::isfinite(amplitude) || !std::isfinite(phase))
            {
                return juce::Result::fail("Vocoder filter spectrum overflowed at frame " + juce::String(static_cast<juce::int64>(frame)) + ".");
            }
            const float filterReal = amplitude * std::cos(phase);
            const float filterImaginary = amplitude * std::sin(phase);
            const float signalReal = signal[bin * 2];
            const float signalImaginary = signal[bin * 2 + 1];
            signal[bin * 2] = signalReal * filterReal - signalImaginary * filterImaginary;
            signal[bin * 2 + 1] = signalReal * filterImaginary + signalImaginary * filterReal;
        }
        // JUCE's inverse already applies 1/N, equivalent to the reference's unnormalised real inverse followed by 2/N.
        fft.performRealOnlyInverseTransform(signal.data());
        const auto destinationStart = frameSample - static_cast<std::int64_t>(fftSize / 2);
        for (std::size_t sample = 0; sample < fftSize; ++sample)
        {
            const auto destinationSample = destinationStart + static_cast<std::int64_t>(sample);
            if (destinationSample >= 0 && destinationSample < static_cast<std::int64_t>(rendered.size()))
            {
                auto& value = rendered[static_cast<std::size_t>(destinationSample)];
                value += signal[sample] * getBoundaryWindow(sample, fftSize);
                if (!std::isfinite(value))
                {
                    return juce::Result::fail("Vocoder filter produced a non-finite output at frame " + juce::String(static_cast<juce::int64>(frame)) + ".");
                }
            }
        }
    }
    output = std::move(rendered);
    return juce::Result::ok();
}
} // namespace sv::synthesis
