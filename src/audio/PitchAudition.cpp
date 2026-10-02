#include "PitchAudition.h"

#include <algorithm>
#include <cmath>

namespace sv::audio
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

PitchAudition::PitchAudition()
{
    oscillator.initialise([](float phase)
                          { return std::sin(phase); },
                          2048);
}

void PitchAudition::setPitch(int midiPitch)
{
    if (midiPitch < 0 || midiPitch > 127)
    {
        requestedKey.fetch_and(~heldFlag, std::memory_order_release);
        return;
    }

    const auto generation = (requestedKey.load(std::memory_order_relaxed) + generationStep) & ~(generationStep - 1);
    requestedKey.store(generation | heldFlag | static_cast<std::uint64_t>(midiPitch), std::memory_order_release);
}

void PitchAudition::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    oscillator.prepare({sampleRate, 1, 1});
    envelope.setSampleRate(sampleRate);
    envelope.setParameters({0.004f, 0.25f, 0.45f, 0.05f});
    reset();
}

void PitchAudition::reset()
{
    requestedKey.store(0, std::memory_order_release);
    consumedGeneration = 0;
    minimumHoldSamples = 0;
    keyOn = false;
    oscillator.reset();
    envelope.reset();
}

void PitchAudition::addToOutput(float* const* channels, int numChannels, int numSamples)
{
    const auto key = requestedKey.load(std::memory_order_acquire);
    const auto generation = key & ~(generationStep - 1);
    if (generation != consumedGeneration)
    {
        consumedGeneration = generation;
        const auto frequency = juce::MidiMessage::getMidiNoteInHertz(static_cast<int>(key & 0x7f));
        oscillator.setFrequency(static_cast<float>(std::min(frequency, sampleRate * 0.45)), true);
        envelope.noteOn();
        keyOn = true;
        // Preserve even a click whose press and release arrive between callbacks.
        minimumHoldSamples = static_cast<int>(std::ceil(sampleRate * 0.03));
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if ((key & heldFlag) == 0 && keyOn && minimumHoldSamples == 0)
        {
            envelope.noteOff();
            keyOn = false;
        }
        if (!envelope.isActive())
        {
            break;
        }
        minimumHoldSamples = std::max(0, minimumHoldSamples - 1);
        const float value = 0.18f * envelope.getNextSample() * oscillator.processSample(0.0f);
        for (int channel = 0; channel < numChannels; ++channel)
        {
            if (channels[channel] != nullptr)
            {
                channels[channel][sample] = std::clamp(channels[channel][sample] + value, -1.0f, 1.0f);
            }
        }
    }
}
} // namespace sv::audio
