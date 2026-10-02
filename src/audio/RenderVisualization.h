#pragma once

#include "core/Project.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sv::audio
{
struct WaveformPeak
{
    float minimum = 0.0f;
    float maximum = 0.0f;
};

struct WaveformLevel
{
    std::size_t samplesPerPeak = 64;
    std::vector<WaveformPeak> peaks;
};

// Immutable native-rate phrase data, shared by the PCM cache and message thread.
// No display data is accessed or released by the audio callback.
struct PhraseVisualization
{
    double sampleRate = 0.0;
    double frameIntervalSeconds = 0.0;
    std::size_t sampleCount = 0;
    float peakMagnitude = 0.0f;
    // Continuous synthesis control pitch at frame centres, before voicing is
    // applied to excitation. Unvoiced consonants do not break this curve.
    std::vector<float> midiPitch;
    // Each level combines adjacent peaks from the preceding level.
    std::vector<WaveformLevel> waveform;

    [[nodiscard]] std::size_t getBytes() const noexcept;
};

struct VisualNote
{
    NoteId id = 0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int pitch = 60;
    int pitchOffset = 0;
    bool mainReference = false;
};

struct VisualPhrase
{
    // Native PCM/F0 origin, including synthesis context before project zero.
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::vector<VisualNote> notes;
    std::shared_ptr<const PhraseVisualization> data;
};

struct TrackVisualization
{
    std::string mainGroupId;
    std::vector<VisualPhrase> phrases;
};

struct RenderVisualization
{
    // Completed display data stays visible across edits within this document.
    std::uint64_t documentGeneration = 0;
    std::vector<TrackVisualization> tracks;
};
} // namespace sv::audio
