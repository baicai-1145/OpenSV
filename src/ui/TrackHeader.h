#pragma once

#include "core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace sv
{
class TrackHeader final : public juce::Component
{
public:
    TrackHeader(ProjectDocument& document, int trackIndex);
    ~TrackHeader() override;

    [[nodiscard]] bool matches(std::uint64_t generation, const std::string& groupId) const;
    void refresh();

    std::function<void(int)> onChooseVoice;
    std::function<void(int)> onShowTrackSettings;
    std::function<void(int)> onRename;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    class MixerButton final : public juce::TextButton
    {
    public:
        explicit MixerButton(const juce::String& label);
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
    };

    class VoiceButton final : public juce::Button
    {
    public:
        VoiceButton();
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
    };

    class KnobLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider(juce::Graphics& graphics, int x, int y, int width, int height, float position, float startAngle, float endAngle, juce::Slider& slider) override;
    };

    [[nodiscard]] int findTrackIndex() const;
    [[nodiscard]] int selectTrack();
    void refreshVoice(const Track& track);
    void beginMixDrag(bool gain);
    void finishMixDrag(bool gain);
    void applyMix(bool gain, std::optional<double> originalValue);
    void updateMixTooltips();

    ProjectDocument& document;
    const std::uint64_t generation;
    const std::string groupId;
    KnobLookAndFeel knobLookAndFeel;
    juce::Slider gainSlider;
    juce::Slider panSlider;
    MixerButton muteButton{"M"};
    MixerButton soloButton{"S"};
    juce::DrawableButton voiceIcon{juce::String::fromUTF8("选择声库"), juce::DrawableButton::ImageFitted};
    juce::DrawableButton settingsButton{juce::String::fromUTF8("音轨属性"), juce::DrawableButton::ImageFitted};
    VoiceButton voiceButton;
    juce::String trackName;
    std::optional<std::string> voicePath;
    std::optional<double> gainDragOrigin;
    std::optional<double> panDragOrigin;
    bool selected = false;
};
} // namespace sv
