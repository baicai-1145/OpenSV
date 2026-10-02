#pragma once

#include "Project.h"

#include <juce_core/juce_core.h>

namespace sv
{
// Import replaces the project only after the complete file has been validated.
[[nodiscard]] juce::Result importMidiFile(const juce::File& file, Project& project);
[[nodiscard]] juce::Result exportMidiFile(const juce::File& file, const Project& project);
} // namespace sv
