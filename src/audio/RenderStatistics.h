#pragma once

#include "synthesis/SynthesisStatistics.h"

#include <cstddef>

namespace sv::audio
{
struct RenderStatistics
{
    std::size_t totalPhrases = 0;
    std::size_t renderedPhrases = 0;
    std::size_t reusedPhrases = 0;
    std::size_t cachedAudioBytes = 0;
    std::size_t cachedInferenceBytes = 0;
    double elapsedMilliseconds = 0.0;
    double modelLoadMilliseconds = 0.0;
    double durationMilliseconds = 0.0;
    synthesis::SynthesisStatistics synthesis;
};
} // namespace sv::audio
