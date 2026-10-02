#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <functional>

namespace sv::audio
{
// Writes 24-bit stereo PCM from a non-realtime thread. The caller confirms
// replacement; failed or cancelled writes preserve an existing destination.
[[nodiscard]] juce::Result writeWaveFile(const juce::File& file, const juce::AudioBuffer<float>& samples, double sampleRate, const std::function<bool()>& shouldCancel = {});
} // namespace sv::audio
