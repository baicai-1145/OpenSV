#include "WaveFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>

namespace sv::audio
{
juce::Result writeWaveFile(const juce::File& file, const juce::AudioBuffer<float>& samples, double sampleRate, const std::function<bool()>& shouldCancel)
{
    try
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || samples.getNumChannels() != 2)
        {
            return juce::Result::fail("WAV export requires stereo audio and a positive finite sample rate.");
        }
        if (shouldCancel && shouldCancel())
        {
            return juce::Result::fail("WAV export was cancelled.");
        }

        const juce::TemporaryFile temporary(file);
        std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
        if (stream == nullptr)
        {
            return juce::Result::fail("Cannot create the WAV file: " + file.getFullPathName());
        }

        juce::WavAudioFormat format;
        const auto options = juce::AudioFormatWriterOptions().withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(24);
        auto writer = format.createWriterFor(stream, options);
        if (writer == nullptr)
        {
            return juce::Result::fail("Cannot create the WAV writer: " + file.getFullPathName());
        }
        for (int offset = 0; offset < samples.getNumSamples();)
        {
            if (shouldCancel && shouldCancel())
            {
                return juce::Result::fail("WAV export was cancelled.");
            }
            const int samplesToWrite = std::min(4096, samples.getNumSamples() - offset);
            if (!writer->writeFromAudioSampleBuffer(samples, offset, samplesToWrite))
            {
                return juce::Result::fail("Cannot write the WAV audio data: " + file.getFullPathName());
            }
            offset += samplesToWrite;
        }
        if (!writer->flush())
        {
            return juce::Result::fail("Cannot finalise the WAV file: " + file.getFullPathName());
        }
        writer.reset();

        if (shouldCancel && shouldCancel())
        {
            return juce::Result::fail("WAV export was cancelled.");
        }
        if (!temporary.overwriteTargetFileWithTemporary())
        {
            return juce::Result::fail("Cannot replace the WAV file: " + file.getFullPathName());
        }
        return juce::Result::ok();
    }
    catch (const std::exception& exception)
    {
        return juce::Result::fail("WAV export failed: " + juce::String(exception.what()));
    }
    catch (...)
    {
        return juce::Result::fail("WAV export failed with an unexpected error.");
    }
}
} // namespace sv::audio
