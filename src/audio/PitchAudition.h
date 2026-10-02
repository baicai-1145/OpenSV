#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>

namespace sv::audio
{
// The message thread publishes the key state. Only the audio thread touches
// the oscillator/envelope; prepare/reset run while its callback is stopped.
class PitchAudition final
{
public:
    PitchAudition();

    void setPitch(int midiPitch);
    void prepare(double sampleRate);
    void reset();
    void addToOutput(float* const* channels, int numChannels, int numSamples);

private:
    static constexpr std::uint64_t heldFlag = 0x80;
    static constexpr std::uint64_t generationStep = 0x100;
    std::atomic<std::uint64_t> requestedKey{0};
    std::uint64_t consumedGeneration = 0;
    juce::dsp::Oscillator<float> oscillator;
    juce::ADSR envelope;
    double sampleRate = 48000.0;
    int minimumHoldSamples = 0;
    bool keyOn = false;
};
} // namespace sv::audio
