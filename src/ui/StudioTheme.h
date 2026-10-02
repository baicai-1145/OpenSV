#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sv
{
namespace colours
{
inline const juce::Colour background{0xff2d2d2d};
inline const juce::Colour panel{0xff383838};
inline const juce::Colour raised{0xff454545};
inline const juce::Colour border{0xff202020};
inline const juce::Colour text{0xffd7d7d7};
inline const juce::Colour subdued{0xff999999};
inline const juce::Colour accent{0xff80ad30};
} // namespace colours

class StudioTheme final : public juce::LookAndFeel_V4
{
public:
    StudioTheme();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) override;
};
} // namespace sv
