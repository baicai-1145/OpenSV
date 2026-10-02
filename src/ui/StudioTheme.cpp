#include "StudioTheme.h"

namespace sv
{
StudioTheme::StudioTheme()
{
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName("Microsoft YaHei UI");
#elif JUCE_MAC
    setDefaultSansSerifTypefaceName("PingFang SC");
#endif
    setColour(juce::ResizableWindow::backgroundColourId, colours::background);
    setColour(juce::TextButton::buttonColourId, colours::raised);
    setColour(juce::TextButton::buttonOnColourId, colours::accent);
    setColour(juce::TextButton::textColourOffId, colours::text);
    setColour(juce::TextButton::textColourOnId, colours::border);
    setColour(juce::Label::textColourId, colours::text);
    setColour(juce::TextEditor::backgroundColourId, colours::background);
    setColour(juce::TextEditor::textColourId, colours::text);
    setColour(juce::TextEditor::outlineColourId, colours::border);
    setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour(juce::TextEditor::highlightColourId, colours::accent.withAlpha(0.35f));
    setColour(juce::ComboBox::backgroundColourId, colours::background);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::outlineColourId, colours::border);
    setColour(juce::PopupMenu::backgroundColourId, colours::panel);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour(juce::PopupMenu::highlightedTextColourId, colours::border);
    setColour(juce::Slider::thumbColourId, colours::accent);
    setColour(juce::Slider::trackColourId, colours::accent.withAlpha(0.65f));
    setColour(juce::Slider::backgroundColourId, colours::border);
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, colours::background);
    setColour(juce::Slider::textBoxOutlineColourId, colours::border);
}

void StudioTheme::drawButtonBackground(juce::Graphics& graphics, juce::Button& button, const juce::Colour& base, bool highlighted, bool down)
{
    auto colour = button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId) : base;
    if (down)
    {
        colour = colour.darker(0.15f);
    }
    else if (highlighted)
    {
        colour = colour.brighter(0.1f);
    }
    graphics.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.35f));
    graphics.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(0.5f), 3.0f);
}

juce::Font StudioTheme::getTextButtonFont(juce::TextButton&, int)
{
    return juce::Font(juce::FontOptions(13.0f));
}

juce::Font StudioTheme::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(13.0f));
}

juce::Font StudioTheme::getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&)
{
    return juce::Font(juce::FontOptions(16.0f));
}
} // namespace sv
