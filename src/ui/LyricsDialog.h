#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace sv
{
class LyricsDialog
{
public:
    static void show(juce::Component& parent, const juce::String& initialLyrics, int noteCount, std::function<void(const juce::StringArray&, bool)> onApply);
};
} // namespace sv
