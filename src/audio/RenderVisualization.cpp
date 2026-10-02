#include "RenderVisualization.h"

#include <cstddef>

namespace sv::audio
{
std::size_t PhraseVisualization::getBytes() const noexcept
{
    std::size_t bytes = sizeof(PhraseVisualization) + midiPitch.capacity() * sizeof(float) + waveform.capacity() * sizeof(WaveformLevel);
    for (const auto& level : waveform)
    {
        bytes += level.peaks.capacity() * sizeof(WaveformPeak);
    }
    return bytes;
}
} // namespace sv::audio
