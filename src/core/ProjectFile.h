#pragma once

#include "Project.h"

#include <juce_core/juce_core.h>

namespace sv
{
[[nodiscard]] juce::Result loadProjectFile(const juce::File& file, Project& project);
[[nodiscard]] juce::Result saveProjectFile(const juce::File& file, const Project& project);
} // namespace sv
