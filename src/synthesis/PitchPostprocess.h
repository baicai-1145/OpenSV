#pragma once

#include "PitchFeatures.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <span>

namespace sv::synthesis
{
// Corrects the stable center of long notes while retaining predicted vibrato
// and preserving transitions. Pitch and notes share the padded model timeline.
[[nodiscard]] juce::Result applyPitchPostprocess(std::span<const PitchNote> notes, float frameIntervalSeconds, std::span<float> midiPitch, const std::function<bool()>& shouldCancel = {});
} // namespace sv::synthesis
